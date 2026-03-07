/* mnist.h - Header file for required structs and memory addresses to run
 * MNIST interence with CIM API
 */

#ifndef MNIST_H
#define MNIST_H

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>

/* ---------- MNIST structs ---------- */

#define MNIST_IMAGE_WIDTH 28
#define MNIST_IMAGE_HEIGHT 28
#define MNIST_IMAGE_SIZE (MNIST_IMAGE_WIDTH * MNIST_IMAGE_HEIGHT)
#define MNIST_LABELS 10

// Holds DNN weights and biases, trained on MNIST images
typedef struct neural_network_t_ {
    float b[MNIST_LABELS];
    float W[MNIST_LABELS][MNIST_IMAGE_SIZE];
} neural_network_t;

// 28 x 28 MNIST pixel image with no byte padding
typedef struct mnist_image_t_ {
    uint8_t pixels[MNIST_IMAGE_SIZE];
} __attribute__((packed)) mnist_image_t;

// Holds MNIST dataset of images
typedef struct mnist_dataset_t_ {
    mnist_image_t * images;
    uint8_t * labels;
    uint32_t size;
} mnist_dataset_t;

/* ---------- SRAM layout (CIM Data Regions) ---------- */
/* Addresses are defined as the start of the region packed right after the previous' size */
#define CIM_DATA_REGION  0x00001000   // Marker for the start of CIM data section
#define WEIGHT_BASE_ADDR 0x00001000                                                   // Weights: 10 labels * 784 pixels per image * 4 bytes per float
#define BIAS_BASE_ADDR   (WEIGHT_BASE_ADDR + (MNIST_LABELS * MNIST_IMAGE_SIZE * 4))   // Bias: 10 labels * 4 bytes per float
#define INPUT_BASE_ADDR  (BIAS_BASE_ADDR   + (MNIST_LABELS * 4))                      // Inputs: 784 pixels per image * 4 bytes per float
#define OUTPUT_BASE_ADDR (INPUT_BASE_ADDR  + (MNIST_IMAGE_SIZE * 4))                  

/* Load images dataset */
int load_t10k_dataset(const char *images_path, const char *labels_path, mnist_dataset_t *out);

/* Deallocate dataset resources */
void free_dataset(mnist_dataset_t *d);

#endif // MNIST_H