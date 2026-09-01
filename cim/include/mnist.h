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

// /* ---------- MNIST structs ---------- */

// #define MNIST_IMAGE_WIDTH 28
// #define MNIST_IMAGE_HEIGHT 28
// #define MNIST_IMAGE_SIZE (MNIST_IMAGE_WIDTH * MNIST_IMAGE_HEIGHT)
// #define MNIST_LABELS 10

// ==========================================
// NETWORK TOGGLE
// 0 = MNIST (784 x 10)
// 1 = Large Synthetic NN (1024 x 1024)
// ==========================================
#if USE_SYNTHETIC_NETWORK
    #define NN_IN_SIZE 1024
    #define NN_OUT_SIZE 1024
#else
    #define NN_IN_WIDTH 28
    #define NN_IN_HEIGHT 28
    #define NN_IN_SIZE (NN_IN_WIDTH * NN_IN_HEIGHT)  // 784
    #define NN_OUT_SIZE 10
#endif

// Holds DNN weights and biases, trained on MNIST images
typedef struct neural_network_t_ {
    float b[NN_OUT_SIZE];
    float W[NN_OUT_SIZE][NN_IN_SIZE];
} neural_network_t;

/* 4-bit quantized neural network 
 * Weights are signed s4 in int8, activations are unsigned u4 in uint8)
 * Stored scales make the file self-describing and reproducible.
 */
typedef struct neural_network_q4_t_ {
    float x_scale;                             // float value per 1 LSB of x_q (u4)
    float w_scale[NN_OUT_SIZE];                // per-output-channel weight scale
    int32_t b[NN_OUT_SIZE];                    // bias in accumulator domain
    int8_t  W[NN_OUT_SIZE][NN_IN_SIZE];        // signed-4b stored in int8 [-8..7]
} __attribute__((packed)) neural_network_q4_t;

// 28 x 28 MNIST pixel image with no byte padding
typedef struct mnist_image_t_ {
    uint8_t pixels[NN_IN_SIZE];
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
#define WEIGHT_SIZE      (NN_OUT_SIZE * NN_IN_SIZE)

#define BIAS_BASE_ADDR   (WEIGHT_BASE_ADDR + WEIGHT_SIZE)           
#define BIAS_SIZE        (NN_OUT_SIZE * sizeof(int32_t))           

#define INPUT_BASE_ADDR  (BIAS_BASE_ADDR + BIAS_SIZE)               
#define INPUT_SIZE       (NN_IN_SIZE)                         

#define OUTPUT_BASE_ADDR (INPUT_BASE_ADDR + INPUT_SIZE)             
#define OUTPUT_SIZE      (NN_OUT_SIZE * sizeof(int32_t))

// ---------- Public functions ----------
/* Load images dataset */
int load_t10k_dataset(const char *images_path, const char *labels_path, mnist_dataset_t *out);

/* Deallocate dataset resources */
void free_dataset(mnist_dataset_t *d);

#endif // MNIST_H