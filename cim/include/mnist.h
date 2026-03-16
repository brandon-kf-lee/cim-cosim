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

/* 4-bit quantized neural network 
 * Weights are signed s4 in int8, activations are unsigned u4 in uint8)
 * Stored scales make the file self-describing and reproducible.
 */
typedef struct neural_network_q4_t_ {
    float x_scale;                              // float value per 1 LSB of x_q (u4)
    float w_scale[MNIST_LABELS];                // per-output-channel weight scale
    int32_t b[MNIST_LABELS];                    // bias in accumulator domain
    int8_t  W[MNIST_LABELS][MNIST_IMAGE_SIZE];  // signed-4b stored in int8 [-8..7]
} __attribute__((packed)) neural_network_q4_t;

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
/* SRAM addresses are abstracted away for now, may not represent where the data (weights, input) should be stored in a real CIM system 
   Addresses are defined as the start of the region packed right after the previous' size 
 */
#define CIM_DATA_REGION  0x00001000   // Marker for the start of CIM data section

#define WEIGHT_BASE_ADDR 0x00001000
#define WEIGHT_SIZE      (MNIST_LABELS * MNIST_IMAGE_SIZE)          // 7840 (4 bits in a 1 byte holder weight)

#define BIAS_BASE_ADDR   (WEIGHT_BASE_ADDR + WEIGHT_SIZE)           // +7840
#define BIAS_SIZE        (MNIST_LABELS * sizeof(int32_t))           // 40 (4 byte bias)

#define INPUT_BASE_ADDR  (BIAS_BASE_ADDR + BIAS_SIZE)               // +40
#define INPUT_SIZE       (MNIST_IMAGE_SIZE)                         // 784 (4 bits in a 1 byte holder input)

#define OUTPUT_BASE_ADDR (INPUT_BASE_ADDR + INPUT_SIZE)             // +784
#define OUTPUT_SIZE      (MNIST_LABELS * sizeof(int32_t))           // 40 (4 byte output)

// ---------- Public functions ----------
/* Load images dataset */
int load_t10k_dataset(const char *images_path, const char *labels_path, mnist_dataset_t *out);

/* Deallocate dataset resources */
void free_dataset(mnist_dataset_t *d);

#endif // MNIST_H