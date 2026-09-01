// neural_network.h

#ifndef NEURAL_NETWORK_H
#define NEURAL_NETWORK_H

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

// Holds DNN weights and biases (MNIST trained on MNIST images)
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

#endif // NEURAL_NETWORK_H