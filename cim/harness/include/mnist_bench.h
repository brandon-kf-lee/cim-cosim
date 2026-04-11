#ifndef MNIST_BENCH_H
#define MNIST_BENCH_H

#include "mnist.h"
#include <stdint.h>

/* Defaults */
#define MNIST_BENCH_DEFAULT_NETWORK     "binaries/mnist_network.bin"
#define MNIST_BENCH_DEFAULT_IMAGE_0     "binaries/MNIST_0.bin"
#define MNIST_BENCH_DEFAULT_T10K_IMAGES "binaries/t10k-images-idx3-ubyte"
#define MNIST_BENCH_DEFAULT_T10K_LABELS "binaries/t10k-labels-idx1-ubyte"

typedef enum { SEC_TOTAL = 0, SEC_OVERHEAD, SEC_SETUP } section_t;

typedef struct mnist_bench_opts_t {
    const char *path_network;
    const char *path_image;
    const char *path_t10k_images;
    const char *path_t10k_labels;

    section_t section;

    uint64_t iters;
    uint64_t warmup;
    int verbose;
} mnist_bench_opts_t;

/* Parse argv into opts (sets defaults). Returns 0 on success, 2 on usage error. */
int mnist_bench_parse_args(mnist_bench_opts_t *opts, int argc, char **argv);

/* Shared helpers */
void die_errno(const char *msg);
int  read_exact_file(const char *path, void *dst, size_t size);
void neural_network_softmax(float *a, int n);
int  argmax_f32(const float *a, int n);
uint8_t pixel_to_u4(uint8_t p);
void quantize_image_to_u4(const mnist_image_t *img, uint8_t out_q[MNIST_IMAGE_SIZE]);
uint64_t now_ns(void);

#endif // MNIST_BENCH_H