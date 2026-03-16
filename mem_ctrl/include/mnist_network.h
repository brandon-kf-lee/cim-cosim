// mnist_network.h

#ifndef MNIST_NETWORK_H
#define MNIST_NETWORK_H

#define MNIST_IMAGE_WIDTH 28
#define MNIST_IMAGE_HEIGHT 28
#define MNIST_IMAGE_SIZE MNIST_IMAGE_WIDTH * MNIST_IMAGE_HEIGHT
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

// 28 x 28 pixel image with no byte padding
typedef struct mnist_image_t_ {
    uint8_t pixels[MNIST_IMAGE_SIZE];
} __attribute__((packed)) mnist_image_t;

#endif // MNIST_NETWORK_H