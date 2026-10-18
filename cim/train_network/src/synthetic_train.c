/* 
 * Code to generate a (pre-quantized) synthetic neural network
 * Author: Brandon Lee, brandon.kf.lee@gmail.com
 *     Derived from: Andrew Carter, https://github.com/AndrewCarterUK/mnist-neural-network-plain-c
 */ 

// Force the large network macros for this generator
#define USE_SYNTHETIC_NETWORK 1

#include "include/mnist_file.h"
#include "include/neural_network.h"

int main(void)
{
    neural_network_q4_t network_q4;

    // Initialise weights and biases with random values
    neural_network_q4_random_weights(&network_q4);

    // Save network into external file as a binary
    char * file_name = "binaries/synthetic_network_q4.bin";
    FILE * synthetic_network = fopen(file_name, "wb");
    fwrite (&network_q4, sizeof(network_q4), 1, synthetic_network);
    printf("Neural network saved as \"%s\"\n", file_name);
    fclose(synthetic_network);

    return 0;
}
