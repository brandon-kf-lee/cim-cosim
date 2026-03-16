/*
 * mnist_CPU_FP32.c - MNIST inference using ONLY CPU calculations
 * This version of mnist_CPU does not use quantized weights or inputs. 
 * It is mainly used just to provide an accuracy baseline, with full precision
 * inference (32-bit) vs quantized (4-bit)
 */

#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "cim.h"
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

void normalize_image_to_f32(const mnist_image_t *img, float out_f[MNIST_IMAGE_SIZE]) {
    for (int j = 0; j < MNIST_IMAGE_SIZE; j++)
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
    
    float input_f[MNIST_IMAGE_SIZE];
    float activations[MNIST_LABELS];
    /* ---- Perform inference ---- */
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
    free_dataset(&dataset);
    return 0;
}