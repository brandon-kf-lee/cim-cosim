/*
 * mnist_CIM_inst.c - MNIST inference using CIM userspace library (INT4/INT4 path)
 * with instret (RISC-V instructions retired) gating around chosen regions.
 *
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
#include <sys/mman.h>

int main(int argc, char **argv)
{
    mnist_bench_opts_t opts;
    int prc = mnist_bench_parse_args(&opts, argc, argv);
    if (prc != 0) return prc;

    /* ---------------- Simulation Overhead ---------------- */
    // Load Quantized Network (heap allocate for better chances of contiguous mem allocation)
    neural_network_q4_t *network_q4 = NULL;
    if (posix_memalign((void **)&network_q4, 4096, sizeof(neural_network_q4_t)) != 0) {
        perror("posix_memalign network");
        return 1;
    }
    if (mlock(network_q4, sizeof(neural_network_q4_t)) != 0) perror("mlock network");

    if (read_exact_file(opts.path_network, network_q4, sizeof(*network_q4)) != 0)
        return 1;

    // Prep Variables
    cim_dev_t *dev = NULL;              // CIM device
    mnist_dataset_t dataset;            // MNIST image dataset
    int rc = 0;                         // CIM return values
    uint8_t *input_q = NULL;            // Quantized image input
    int32_t *logits  = NULL;            // Raw output
    uint64_t input_q_phys, logits_phys; // Physical addresses
    float k[NN_OUT_SIZE];               // Dequantization scaling factors

    // Align allocated memory to page size
    if (posix_memalign((void **)&input_q, 4096, NN_IN_SIZE) != 0 || !input_q) {
        perror("posix_memalign input_q");
        goto out;
    }
    if (posix_memalign((void **)&logits, 4096, sizeof(int32_t) * NN_OUT_SIZE) != 0 || !logits) {
        perror("posix_memalign logits");
        goto out;
    }
    
    // Lock virtual address space into RAM to prevent paging
    if (mlock(input_q, NN_IN_SIZE) != 0) perror("mlock input_q");
    if (mlock(logits, sizeof(int32_t) * NN_OUT_SIZE) != 0) perror("mlock logits");

    memset(input_q, 0, NN_IN_SIZE);
    memset(logits,  0, sizeof(int32_t) * NN_OUT_SIZE);

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

    // CIM Init
    cim_config_t cfg = {0};
    cfg.dma_mode = CIM_DMA_PAGED;
    cfg.debug = 0;

    rc = cim_init(&dev, &cfg);
    if (rc != CIM_OK) {
        fprintf(stderr, "cim_init failed: %s (%d)\n", cim_strerror(rc), rc);
        free_dataset(&dataset);
        return 1;
    }

    // Convert image's virtual address to physical 
    rc = io_virt_to_phys(dev, input_q, logits, &input_q_phys, &logits_phys);
    if (rc != CIM_OK) goto out;

    // Pre-configure input & output locations, since it doesn't change between inferences
    rc = cim_configure_inference(dev,
                                 input_q_phys, NN_IN_SIZE,
                                 logits_phys, sizeof(int32_t) * NN_OUT_SIZE,
                                 INPUT_BASE_ADDR, OUTPUT_BASE_ADDR);
    if (rc != CIM_OK) goto out;    

    /* ---------------- Weight & Bias DMA Region ---------------- */
    rc = cim_dma_write_sram(dev, WEIGHT_BASE_ADDR, network_q4->W, sizeof(network_q4->W));
    if (rc != CIM_OK) { fprintf(stderr, "DMA weights failed: %s (%d)\n", cim_strerror(rc), rc); goto out; }

    rc = cim_dma_write_sram(dev, BIAS_BASE_ADDR, network_q4->b, sizeof(network_q4->b));
    if (rc != CIM_OK) { fprintf(stderr, "DMA bias failed: %s (%d)\n", cim_strerror(rc), rc); goto out; }

    /* Pre-compute dequantization scaling factors (reduces work done in main loop)
       Necessary for comparable scores with per-class w_scale */
    for (int i = 0; i < NN_OUT_SIZE; i++) {
        k[i] = network_q4->x_scale * network_q4->w_scale[i];
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

        // Start and wait for asynchronous inference
        cim_start_inference(dev);
        rc = cim_wait_inference(dev);
        if (rc != CIM_OK) goto out;

        // Argmax with integrated per-class dequantization
        int pred = 0;
        float best = (float)logits[0] * k[0];
        for (int i = 1; i < NN_OUT_SIZE; i++) {
            float s = (float)logits[i] * k[i];
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
    

    /* ---- calculate deltas and print (outside measurement) ---- */
    printf("instret: setup instructions=     %" PRIu64 "\n", setup_end - setup_start);
    printf("instret: inference instructions= %" PRIu64 "\n", infr_end - infr_start);

#if !USE_SYNTHETIC_NETWORK
    printf("accuracy: %" PRIu64 "/%" PRIu64 " = %.2f%%\n",
           correct, total, total ? (100.0 * (double)correct / (double)total) : 0.0);
#else
    printf("Synthetic Inference Complete.\n");
#endif

out:
    free_dataset(&dataset);
    if (dev) cim_close(dev);

    if (network_q4) free(network_q4);
    if (input_q) free(input_q);
    if (logits) free(logits);

    if (rc != CIM_OK) {
        fprintf(stderr, "ERROR: CIM failure: %s (%d)\n", cim_strerror(rc), rc);
        return 1;
    }
    return 0;
}