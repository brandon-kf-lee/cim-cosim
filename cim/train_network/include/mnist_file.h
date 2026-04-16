/* 
 * Defines structs and functions for reading MNIST data
 * Author: Brandon Lee, brandon.kf.lee@gmail.com
 *     Derived from: Andrew Carter, https://github.com/AndrewCarterUK/mnist-neural-network-plain-c
 */ 

#ifndef MNIST_FILE_H_
#define MNIST_FILE_H_

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

// ==========================================
// NETWORK TOGGLE
// 0 = MNIST (784 x 10)
// 1 = Large Synthetic NN (1024 x 1024)
// ==========================================
#define USE_SYNTHETIC_NETWORK 0

#if USE_SYNTHETIC_NETWORK
    #define NN_IN_SIZE 1024
    #define NN_OUT_SIZE 1024
#else
    #define NN_IN_WIDTH 28
    #define NN_IN_HEIGHT 28
    #define NN_IN_SIZE NN_IN_WIDTH * NN_IN_HEIGHT  // 784
    #define NN_OUT_SIZE 10
#endif

#define MNIST_LABEL_MAGIC 0x00000801
#define MNIST_IMAGE_MAGIC 0x00000803

typedef struct mnist_label_file_header_t_ {
    uint32_t magic_number;
    uint32_t number_of_labels;
} __attribute__((packed)) mnist_label_file_header_t;

typedef struct mnist_image_file_header_t_ {
    uint32_t magic_number;
    uint32_t number_of_images;
    uint32_t number_of_rows;
    uint32_t number_of_columns;
} __attribute__((packed)) mnist_image_file_header_t;

typedef struct mnist_image_t_ {
    uint8_t pixels[NN_IN_SIZE];
} __attribute__((packed)) mnist_image_t;

typedef struct mnist_dataset_t_ {
    mnist_image_t * images;
    uint8_t * labels;
    uint32_t size;
} mnist_dataset_t;

mnist_dataset_t * mnist_get_dataset(const char * image_path, const char * label_path);
void mnist_free_dataset(mnist_dataset_t * dataset);
int mnist_batch(mnist_dataset_t * dataset, mnist_dataset_t * batch, int batch_size, int batch_number);

#endif
