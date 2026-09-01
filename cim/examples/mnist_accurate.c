/*
 * mnist_accurate.c - MNIST inference using ONLY CPU calculations
 * This version of mnist_CPU does not use quantized weights or inputs. 
 * It is mainly used just to provide an accuracy baseline, with full precision
 * inference (32-bit) vs quantized (4-bit)
 */

#define _POSIX_C_SOURCE 200809L

#include "mnist.h"
#include "mnist_bench.h"

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
                                      float activations[NN_OUT_SIZE])
{
    for (int i = 0; i < NN_OUT_SIZE; i++) {
        float sum = network->b[i];
        for (int j = 0; j < NN_IN_SIZE; j++) {
            /* CPU path scales pixels to [0,1] */
            sum += network->W[i][j] * image[j];
        }
        activations[i] = sum;
    }
}

void normalize_image_to_f32(const mnist_image_t *img, float out_f[NN_IN_SIZE]) {
    for (int j = 0; j < NN_IN_SIZE; j++)
        out_f[j] = ((float)img->pixels[j]) / 255.0f;
}

int main(int argc, char **argv)
{
    mnist_bench_opts_t opts;
    int prc = mnist_bench_parse_args(&opts, argc, argv);
    if (prc != 0) return prc;

    /* ---- load network  ---- */
    neural_network_t network;
    if (read_exact_file(opts.path_network, &network, sizeof(network)) != 0)
        return 1;

    /* ---- load image(s)  ---- */
    mnist_dataset_t dataset;
    memset(&dataset, 0, sizeof(dataset));

    if (load_t10k_dataset(opts.path_t10k_images, opts.path_t10k_labels, &dataset) != 0)
        return 1;
    
    float input_f[NN_IN_SIZE];
    float activations[NN_OUT_SIZE];
    /* ---- Perform inference ---- */
    uint64_t correct = 0, total = 0;
    for (uint64_t it = 0; it < opts.iters; it++) {
        
        // Load image from dataset 
        uint32_t idx = (uint32_t)(it % dataset.size);
        const mnist_image_t *img = &dataset.images[idx];
        int label = dataset.labels[idx];

        // Normalize image and compute 
        normalize_image_to_f32(img, input_f);
        neural_network_hypothesis(input_f, &network, activations);
        
        int pred = argmax_f32(activations, NN_OUT_SIZE);

        // Increment correct predictions
        total++;
        if (pred == label) correct++;
        
        // Print out each image prediction on verbose
        if (opts.verbose) {
            printf("it=%" PRIu64 " label=%d pred=%d\n", it, label, pred);
        }
    }

    printf("accuracy: %" PRIu64 "/%" PRIu64 " = %.2f%%\n",
           correct, total, total ? (100.0 * (double)correct / (double)total) : 0.0);

    free_dataset(&dataset);
    return 0;
}