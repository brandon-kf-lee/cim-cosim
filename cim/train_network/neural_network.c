/* 
 * Implements training and prediction routines for a simple neural network
 * Author: Brandon Lee, brandon.kf.lee@gmail.com
 *     Derived from: Andrew Carter, https://github.com/AndrewCarterUK/mnist-neural-network-plain-c
 */ 

#include "include/mnist_file.h"
#include "include/neural_network.h"

// Convert a pixel value from 0-255 to one from 0 to 1
#define PIXEL_SCALE(x) (((float) (x)) / 255.0f)

// Returns a random value between 0 and 1
#define RAND_FLOAT() (((float) rand()) / ((float) RAND_MAX))

// Returns a random value signed 8 bit value
#define RAND_8b() (((int8_t) rand()) / ((int8_t) RAND_MAX))

/**
 * Initialise the weights and bias vectors with values between 0 and 1
 */
void neural_network_random_weights(neural_network_t * network)
{
    int i, j;

    for (i = 0; i < NN_OUT_SIZE; i++) {
        network->b[i] = RAND_FLOAT();

        for (j = 0; j < NN_IN_SIZE; j++) {
            network->W[i][j] = RAND_FLOAT();
        }
    }
}

/**
 * Initialise the weights and bias vectors with pre-quantized 4b values
 */
void neural_network_q4_random_weights(neural_network_q4_t * network_q4)
{
    int i, j;

    // Fill with dummy but valid data (1)
    network_q4->x_scale = 1.0f / 15.0f;
    for (i = 0; i < NN_OUT_SIZE; i++) {
        network_q4->w_scale[i] = 0.1f;
        network_q4->b[i] = RAND_8b();

        for (j = 0; j < NN_IN_SIZE; j++) {
            network_q4->W[i][j] = RAND_8b(); // 4-bit weight
        }
    }
}

/**
 * Calculate the softmax vector from the activations. This uses a more
 * numerically stable algorithm that normalises the activations to prevent
 * large exponents.
 */
void neural_network_softmax(float * activations, int length)
{
    int i;
    float sum, max;

    for (i = 1, max = activations[0]; i < length; i++) {
        if (activations[i] > max) {
            max = activations[i];
        }
    }

    for (i = 0, sum = 0; i < length; i++) {
        activations[i] = exp(activations[i] - max);
        sum += activations[i];
    }

    for (i = 0; i < length; i++) {
        activations[i] /= sum;
    }
}

/**
 * Use the weights and bias vector to forward propogate through the neural
 * network and calculate the activations.
 */
void neural_network_hypothesis(mnist_image_t * image, neural_network_t * network, float activations[NN_OUT_SIZE])
{
    int i, j;

    for (i = 0; i < NN_OUT_SIZE; i++) {
        activations[i] = network->b[i];

        for (j = 0; j < NN_IN_SIZE; j++) {
            activations[i] += network->W[i][j] * PIXEL_SCALE(image->pixels[j]);
        }
    }

    neural_network_softmax(activations, NN_OUT_SIZE);
}

/**
 * Update the gradients for this step of gradient descent using the gradient
 * contributions from a single training example (image).
 * 
 * This function returns the loss contribution from this training example.
 */
float neural_network_gradient_update(mnist_image_t * image, neural_network_t * network, neural_network_gradient_t * gradient, uint8_t label)
{
    float activations[NN_OUT_SIZE];
    float b_grad, W_grad;
    int i, j;

    // First forward propagate through the network to calculate activations
    neural_network_hypothesis(image, network, activations);

    for (i = 0; i < NN_OUT_SIZE; i++) {
        // This is the gradient for a softmax bias input
        b_grad = (i == label) ? activations[i] - 1 : activations[i];

        for (j = 0; j < NN_IN_SIZE; j++) {
            // The gradient for the neuron weight is the bias multiplied by the input weight
            W_grad = b_grad * PIXEL_SCALE(image->pixels[j]);

            // Update the weight gradient
            gradient->W_grad[i][j] += W_grad;
        }

        // Update the bias gradient
        gradient->b_grad[i] += b_grad;
    }

    // Cross entropy loss
    return 0.0f - log(activations[label]);
}

/**
 * Run one step of gradient descent and update the neural network.
 */
float neural_network_training_step(mnist_dataset_t * dataset, neural_network_t * network, float learning_rate)
{
    neural_network_gradient_t gradient;
    float total_loss;
    uint32_t i, j;

    // Zero initialise gradient for weights and bias vector
    memset(&gradient, 0, sizeof(neural_network_gradient_t));

    // Calculate the gradient and the loss by looping through the training set
    for (i = 0, total_loss = 0; i < dataset->size; i++) {
        total_loss += neural_network_gradient_update(&dataset->images[i], network, &gradient, dataset->labels[i]);
    }

    // Apply gradient descent to the network
    for (i = 0; i < NN_OUT_SIZE; i++) {
        network->b[i] -= learning_rate * gradient.b_grad[i] / ((float) dataset->size);

        for (j = 0; j < NN_IN_SIZE; j++) {
            network->W[i][j] -= learning_rate * gradient.W_grad[i][j] / ((float) dataset->size);
        }
    }

    return total_loss;
}
