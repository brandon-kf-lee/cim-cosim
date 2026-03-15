/*
 * mnist_CPU.c - MNIST inference using ONLY CPU calculations
 * with perf_event_open gating (instructions/cycles) around chosen regions.
 *
 * --measure total : counts (weights+bias DMA once) + (steady-state loop), excludes warmup
 * --measure steady: counts steady-state loop only, excludes warmup
 */

#define _GNU_SOURCE
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

/* CPU forward pass (hypothesis) 
   Images are pre-scaled
 */
static void neural_network_hypothesis(const float *image,
                                      const neural_network_t *network,
                                      float activations[MNIST_LABELS])
{
    for (int i = 0; i < MNIST_LABELS; i++) {
        float sum = network->b[i];
        for (int j = 0; j < MNIST_IMAGE_SIZE; j++) {
            /* CPU path scales pixels to [0,1] */
            sum += network->W[i][j] * image[j];
        }
        activations[i] = sum;
    }
}

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

    /* ---- perf setup ---- */
    perf_gate_t pg;
    if (perf_gate_init(&pg) != 0) die_errno("perf_gate_init (check perf_event permissions)");

    /* CPU has no “device load”, but keep semantics aligned:
       - TOTAL measures warmup-excluded steady-state PLUS a "setup region".
       - Mirror CIM benchmark’s behavior (two regions accumulated with warmup excluded).
    */
    if (opts.meas == MEAS_TOTAL) {
        if (perf_gate_reset_enable(&pg) != 0) die_errno("perf_gate_reset_enable");
        /* (nothing substantial to do here besides any one-time prep) */
        if (perf_gate_disable(&pg) != 0) die_errno("perf_gate_disable");
    }

    /* ---- warmup loop (never measured) ---- */
    float input_f[MNIST_IMAGE_SIZE];
    float activations[MNIST_LABELS];

    for (uint64_t it = 0; it < opts.warmup; it++) {
        const mnist_image_t *img = &single_img;
        if (opts.mode == MODE_T10K) img = &dataset.images[it % dataset.size];

        normalize_image_to_f32(img, input_f);
        neural_network_hypothesis(input_f, &network, activations);
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

        // Normalize image and compute 
        normalize_image_to_f32(img, input_f);
        neural_network_hypothesis(input_f, &network, activations);
        
		// Softmax + argmax
        neural_network_softmax(activations, MNIST_LABELS);
        int pred = argmax_f32(activations, MNIST_LABELS);

        if (opts.verbose) {
            if (opts.mode == MODE_T10K) {
                printf("it=%" PRIu64 " label=%d pred=%d\n", it, label, pred);

                // Increment total & correctness
                total++;
                if (pred == label) correct++;
            } else { 
                printf("it=%" PRIu64 " pred=%d\n", it, pred);
            }
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

    if (opts.verbose && opts.mode == MODE_T10K) {
        printf("accuracy: %" PRIu64 "/%" PRIu64 " = %.2f%%\n",
               correct, total, total ? (100.0 * (double)correct / (double)total) : 0.0);
    }

    perf_gate_close(&pg);
    free_dataset(&dataset);
    return 0;
}