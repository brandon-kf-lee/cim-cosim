/* 
 * Defines structs and functions for the neural network
 * Author: Brandon Lee, brandon.kf.lee@gmail.com
 *     Derived from: Andrew Carter, https://github.com/AndrewCarterUK/mnist-neural-network-plain-c
 */ 

#ifndef NEURAL_NETWORK_H_
#define NEURAL_NETWORK_H_

#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "mnist_file.h"

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

typedef struct neural_network_gradient_t_ {
    float b_grad[NN_OUT_SIZE];
    float W_grad[NN_OUT_SIZE][NN_IN_SIZE];
} neural_network_gradient_t;

void neural_network_random_weights(neural_network_t * network);
void neural_network_q4_random_weights(neural_network_q4_t * network_q4);
void neural_network_hypothesis(mnist_image_t * image, neural_network_t * network, float activations[NN_OUT_SIZE]);
float neural_network_gradient_update(mnist_image_t * image, neural_network_t * network, neural_network_gradient_t * gradient, uint8_t label);
float neural_network_training_step(mnist_dataset_t * dataset, neural_network_t * network, float learning_rate);

#endif
