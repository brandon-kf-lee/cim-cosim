/*
 * mnist_CPU.c - MNIST inference using ONLY CPU calculations (INT4/INT4 path)
 * Matches CIM numeric pipeline for apples-to-apples comparisons:
 *   - activations: u4 in [0..15] via pixel_to_u4()
 *   - weights: s4 stored in int8 [-8..7]
 *   - bias: int32 in accumulator domain
 *   - per-class dequantization for argmax: score[i] = acc[i] * (x_scale * w_scale[i])
 *
 * with perf_event_open gating (instructions/cycles) around chosen regions.
 *
 * --measure total : counts setup + steady-state loop, excludes warmup
 * --measure steady: counts steady-state loop only, excludes warmup
 */

#define _POSIX_C_SOURCE 200809L

#include "mnist.h"
#include "mnist_bench.h"
#include "perf_gate.h"

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* INT4 CPU forward pass: acc_i = b_i + sum_j (W_ij * x_j) */
static void neural_network_hypothesis_q4(const uint8_t x_q[MNIST_IMAGE_SIZE],
                                         const neural_network_q4_t *net,
                                         int32_t acc[MNIST_LABELS])
{
    for (int i = 0; i < MNIST_LABELS; i++) {
        int32_t sum = net->b[i];
        for (int j = 0; j < MNIST_IMAGE_SIZE; j++) {
            sum += (int32_t)net->W[i][j] * (int32_t)x_q[j]; // s4 * u4 -> int32 accumulate
        }
        acc[i] = sum;
    }
}

int main(int argc, char **argv)
{
    mnist_bench_opts_t opts;
    int prc = mnist_bench_parse_args(&opts, argc, argv);
    if (prc != 0) return prc;

    /* ---- load quantized network (outside measurement) ---- */
    neural_network_q4_t network_q4;
    if (read_exact_file(opts.path_network, &network_q4, sizeof(network_q4)) != 0)
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

    /* ---- Input & Output Variables ---- */
    uint8_t input_q[MNIST_IMAGE_SIZE];   // Quantized MNIST input image    
    int32_t acc[MNIST_LABELS];           // Activations

    /* ---- warmup loop (never measured) ---- */
    for (uint64_t it = 0; it < opts.warmup; it++) {
        const mnist_image_t *img = &single_img;
        if (opts.mode == MODE_T10K) img = &dataset.images[it % dataset.size];

        // Quantize MNIST image to 4 bits per pixel
        quantize_image_to_u4(img, input_q);
        neural_network_hypothesis_q4(input_q, &network_q4, acc);
        /* no softmax needed for warmup */
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

        quantize_image_to_u4(img, input_q);
        neural_network_hypothesis_q4(input_q, &network_q4, acc);

        /* Per-class dequantization for comparable scores (required with per-class w_scale) */
        float scores[MNIST_LABELS];
        for (int i = 0; i < MNIST_LABELS; i++) {
            scores[i] = (float)acc[i] * (network_q4.x_scale * network_q4.w_scale[i]);
        }

        int pred = argmax_f32(scores, MNIST_LABELS);

        if (opts.verbose) {
            if (opts.mode == MODE_T10K) {
                printf("it=%" PRIu64 " label=%d pred=%d\n", it, label, pred);

                // Increment total evaluated & correctness
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