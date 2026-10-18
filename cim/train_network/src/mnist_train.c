/* 
 * Glue code that trains the network
 * Author: Brandon Lee, brandon.kf.lee@gmail.com
 *     Derived from: Andrew Carter, https://github.com/AndrewCarterUK/mnist-neural-network-plain-c
 */ 

#include "include/mnist_file.h"
#include "include/neural_network.h"

// Force the large network macros for this generator
#define USE_SYNTHETIC_NETWORK 1
#define STEPS 1000
#define BATCH_SIZE 100

/**
 * Downloaded from: http://yann.lecun.com/exdb/mnist/
 */
const char * train_images_file = "binaries/train-images-idx3-ubyte";
const char * train_labels_file = "binaries/train-labels-idx1-ubyte";

int main(void)
{
    mnist_dataset_t * train_dataset;
    mnist_dataset_t batch;
    neural_network_t network;
    int i, batches;

    // Read the datasets from the files
    train_dataset = mnist_get_dataset(train_images_file, train_labels_file);

    // Initialise weights and biases with random values
    neural_network_random_weights(&network);

    // Calculate how many batches (so we know when to wrap around)
    batches = train_dataset->size / BATCH_SIZE;

    // Train neural network STEPS number of times
    printf("Training neural network in %d steps...\n", STEPS);
    for (i = 0; i < STEPS; i++) {
        // Initialise a new batch
        mnist_batch(train_dataset, &batch, 100, i % batches);

        // Run one step of gradient descent and calculate the loss
        neural_network_training_step(&batch, &network, 0.5);
    }

    // Save network into external file as a binary
    char * file_name = "binaries/mnist_network.bin";
    FILE * mnist_network = fopen(file_name, "wb");
    fwrite (&network, sizeof(network), 1, mnist_network);
    printf("Neural network saved as \"%s\"\n", file_name);
    fclose(mnist_network);

    // Cleanup
    mnist_free_dataset(train_dataset);

    return 0;
}
