/*
 * CPU_inst.c - Inference using ONLY CPU calculations (INT4/INT4 path)
 * with instret (RISC-V instructions retired) gating around chosen regions.
 *
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
static void neural_network_hypothesis_q4(const uint8_t x_q[NN_IN_SIZE],
                                         const neural_network_q4_t *net,
                                         int32_t acc[NN_OUT_SIZE])
{
    for (int i = 0; i < NN_OUT_SIZE; i++) {
        int32_t sum = net->b[i];
        for (int j = 0; j < NN_IN_SIZE; j++) {
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

    /* ---------------- Simulation Overhead ---------------- */
    // Load Quantized Network
    neural_network_q4_t network_q4;
    if (read_exact_file(opts.path_network, &network_q4, sizeof(network_q4)) != 0)
        return 1;

    // Prep Input & Output Variables
    mnist_dataset_t dataset;            // MNIST image dataset
    uint8_t input_q[NN_IN_SIZE];        // Quantized MNIST input image    
    int32_t acc[NN_OUT_SIZE];           // Activations
    float k[NN_OUT_SIZE];               // Dequantization scaling factors

    // Load input
#if USE_SYNTHETIC_NETWORK
    // Synthetic Mode: No dataset to load, just fill the dummy image with 1s
    printf("Running Large Synthetic Network (%dx%d)...\n", NN_IN_SIZE, NN_OUT_SIZE);
    memset(input_q, 1, NN_IN_SIZE); 
#else
    // MNIST Mode: Load dataset
    printf("Running MNIST Network (%dx%d)...\n", NN_IN_SIZE, NN_OUT_SIZE);
    memset(&dataset, 0, sizeof(dataset));
    if (load_t10k_dataset(opts.path_t10k_images, opts.path_t10k_labels, &dataset) != 0) return 1; 
#endif

    /* ---------------- instret Setup ---------------- */
    // Using lighter instret reading to reduce noise when enabling/disabling perf gates
    uint64_t setup_start, setup_end;
    uint64_t infr_start, infr_end;

    if (instret_init() != 0) die_errno("instret_init");

    /* ---------------- Inference Setup ---------------- */
    __asm__ volatile("csrr %0, instret" : "=r"(setup_start));

    /* Pre-compute dequantization scaling factors (reduces work done in main loop)
       Necessary for comparable scores with per-class w_scale */
    for (int i = 0; i < NN_OUT_SIZE; i++) {
        k[i] = network_q4.x_scale * network_q4.w_scale[i];
    }

    __asm__ volatile("csrr %0, instret" : "=r"(setup_end));

    /* ---------------- Inference Region ---------------- */
    __asm__ volatile("csrr %0, instret" : "=r"(infr_start));

    // Accuracy metrics
    uint64_t correct = 0, total = 0;
    int label = 0;
    
    for (uint64_t it = 0; it < opts.iters; it++) {
        
#if !USE_SYNTHETIC_NETWORK
        // Only load new images for MNIST
        uint32_t idx = (uint32_t)(it % dataset.size);
        const mnist_image_t *img = &dataset.images[idx];
        label = dataset.labels[idx];

        // Quantize MNIST image to 4 bits per pixel
        quantize_image_to_u4(img, input_q);
#endif

        neural_network_hypothesis_q4(input_q, &network_q4, acc);

        // Argmax with integrated per-class dequantization
        int pred = 0;
        float best = (float)acc[0] * k[0];
        for (int i = 1; i < NN_OUT_SIZE; i++) {
            float s = (float)acc[i] * k[i];
            if (s > best) { best = s; pred = i; }
        }
        
        // Increment correct predictions
        total++;
        if (pred == label) correct++;
        
        // Print out each image prediction on verbose
        if (opts.verbose) {
            printf("it=%" PRIu64 " label=%d pred=%d\n", it, label, pred);        
        }
    }

    __asm__ volatile("csrr %0, instret" : "=r"(infr_end));


    /* ---- read counters and print (outside measurement) ---- */
    printf("instret: setup instructions=     %" PRIu64 "\n", setup_end - setup_start);
    printf("instret: inference instructions= %" PRIu64 "\n", infr_end - infr_start);

#if !USE_SYNTHETIC_NETWORK
    printf("accuracy: %" PRIu64 "/%" PRIu64 " = %.2f%%\n",
           correct, total, total ? (100.0 * (double)correct / (double)total) : 0.0);
#else
    printf("Synthetic Inference Complete.\n");
#endif

    free_dataset(&dataset);
    return 0;
}