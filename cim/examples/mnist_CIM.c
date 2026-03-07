/*
 * mnist_CIM.c - MNIST inference using CIM userspace library
 * with perf_event_open gating (instructions/cycles) around chosen regions.
 *
 * --measure total : counts (weights+bias DMA once) + (steady-state loop), excludes warmup
 * --measure steady: counts steady-state loop only, excludes warmup
 */

#define _POSIX_C_SOURCE 200809L

#include "cim.h"
#include "mnist.h"
#include "mnist_bench.h"
#include "perf_gate.h"

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    mnist_bench_opts_t opts;
    int prc = mnist_bench_parse_args(&opts, argc, argv);
    if (prc != 0) return prc;

    /* ---- load network (outside measurement) ---- */
    neural_network_t network;
    if (read_exact_file(opts.path_network, &network, sizeof(network)) != 0)
        return 1;

    /* ---- load image(s) (outside measurement) ---- */
    mnist_image_t single_img;
    mnist_dataset_t dataset;
    memset(&dataset, 0, sizeof(dataset));

    if (opts.mode == MODE_SINGLE) {
        if (read_exact_file(opts.path_image, &single_img, sizeof(single_img)) != 0)
            return 1;
    } else {
        if (load_t10k_dataset(opts.path_t10k_images, opts.path_t10k_labels, &dataset) != 0)
            return 1;
    }

    /* ---- init CIM ---- */
    cim_dev_t *dev = NULL;
    cim_config_t cfg = {0};
    cfg.dma_mode = CIM_DMA_PAGED;
    cfg.timeouts.dma_ms = 1000;
    cfg.timeouts.ctrl_ms = 5000;
    cfg.debug = 0;

    int rc = cim_init(&dev, &cfg);
    if (rc != CIM_OK) {
        fprintf(stderr, "cim_init failed: %s (%d)\n", cim_strerror(rc), rc);
        free_dataset(&dataset);
        return 1;
    }

    /* ---- perf setup ---- */
    perf_gate_t pg;
    if (perf_gate_init(&pg) != 0) die_errno("perf_gate_init (check perf_event permissions)");

    /* ---- load weights/bias once (counted only for TOTAL; warmup excluded later) ---- */
    if (opts.meas == MEAS_TOTAL) {
        if (perf_gate_reset_enable(&pg) != 0) die_errno("perf_gate_reset_enable");
    }

    rc = cim_dma_write_sram(dev, WEIGHT_BASE_ADDR, network.W, sizeof(network.W));
    if (rc != CIM_OK) { fprintf(stderr, "DMA weights failed: %s (%d)\n", cim_strerror(rc), rc); goto out; }

    rc = cim_dma_write_sram(dev, BIAS_BASE_ADDR, network.b, sizeof(network.b));
    if (rc != CIM_OK) { fprintf(stderr, "DMA bias failed: %s (%d)\n", cim_strerror(rc), rc); goto out; }

    if (opts.meas == MEAS_TOTAL) {
        /* stop counting so warmup doesn't get included */
        if (perf_gate_disable(&pg) != 0) die_errno("perf_gate_disable");
    }

    /* ---- warmup loop (never measured) ---- */
    float input_f[MNIST_IMAGE_SIZE];
    float activations[MNIST_LABELS];

    for (uint64_t it = 0; it < opts.warmup; it++) {
        const mnist_image_t *img = &single_img;
        if (opts.mode == MODE_T10K) img = &dataset.images[it % dataset.size];

        normalize_image_to_f32(img, input_f);

        rc = cim_dma_write_sram(dev, INPUT_BASE_ADDR, input_f, sizeof(input_f));
        if (rc != CIM_OK) goto out;

        rc = cim_compute(dev);
        if (rc != CIM_OK) goto out;

        for (int i = 0; i < MNIST_LABELS; i++) {
            uint32_t addr = OUTPUT_BASE_ADDR + (uint32_t)(i * sizeof(float));
            rc = cim_read_sram_f32_irq(dev, addr, &activations[i]);
            if (rc != CIM_OK) goto out;
        }
    }

    /* ---- measured steady-state region ---- */
    /* Steady state only: reset counts and start */
    if (opts.meas == MEAS_STEADY) {
        if (perf_gate_reset_enable(&pg) != 0) die_errno("perf_gate_reset_enable");
   
    /* Total measurement: resume from warmup without reset */
    } else if (opts.meas == MEAS_TOTAL) {
        if (perf_gate_enable(&pg) != 0) die_errno("perf_gate_enable");
    }

    uint64_t correct = 0, total = 0;
    for (uint64_t it = 0; it < opts.iters; it++) {
        const mnist_image_t *img = &single_img;
        int label = -1;

        // New image if looping through dataset
        if (opts.mode == MODE_T10K) {
            uint32_t idx = (uint32_t)(it % dataset.size);
            img = &dataset.images[idx];
            label = dataset.labels[idx];
        }

        // Normalize image and DMA to SRAM
        normalize_image_to_f32(img, input_f);
        rc = cim_dma_write_sram(dev, INPUT_BASE_ADDR, input_f, sizeof(input_f));
        if (rc != CIM_OK) goto out;

        // Start compute
        rc = cim_compute(dev);
        if (rc != CIM_OK) goto out;

        // Read output
        rc = cim_dma_read_sram(dev, OUTPUT_BASE_ADDR, activations, sizeof(activations));
        if (rc != CIM_OK) goto out;

        // Softmax + argmax
        neural_network_softmax(activations, MNIST_LABELS);
        int pred = argmax_f32(activations, MNIST_LABELS);

        if (opts.verbose) {
            if (opts.mode == MODE_T10K) printf("it=%" PRIu64 " label=%d pred=%d\n", it, label, pred);
            else                        printf("it=%" PRIu64 " pred=%d\n", it, pred);
        }

        if (opts.mode == MODE_T10K) {
            total++;
            if (pred == label) correct++;
        }
    }

    /* Stop all measurement */
    if (perf_gate_disable(&pg) != 0) die_errno("perf_gate_disable");

    /* ---- read counters and print once (outside measurement) ---- */
    uint64_t instr = 0, cycles = 0;
    if (perf_gate_read(&pg, &instr, &cycles) != 0) die_errno("perf_gate_read");

    printf("perf: instructions=%" PRIu64 " cycles=%" PRIu64 "\n", instr, cycles);
    printf("perf: instructions/iter=%.2f cycles/iter=%.2f\n",
           (double)instr / (double)opts.iters, (double)cycles / (double)opts.iters);

    if (opts.mode == MODE_T10K) {
        printf("accuracy: %" PRIu64 "/%" PRIu64 " = %.2f%%\n",
               correct, total, total ? (100.0 * (double)correct / (double)total) : 0.0);
    }

out:
    perf_gate_close(&pg);
    cim_close(dev);
    free_dataset(&dataset);

    if (rc != CIM_OK) {
        fprintf(stderr, "ERROR: CIM failure: %s (%d)\n", cim_strerror(rc), rc);
        return 1;
    }
    return 0;
}