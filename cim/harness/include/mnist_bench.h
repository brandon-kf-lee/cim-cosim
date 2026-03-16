#ifndef MNIST_BENCH_H
#define MNIST_BENCH_H

#include "mnist.h"
#include <stdint.h>

/* Defaults */
#define MNIST_BENCH_DEFAULT_NETWORK     "binaries/mnist_network.bin"
#define MNIST_BENCH_DEFAULT_IMAGE_0     "binaries/MNIST_0.bin"
#define MNIST_BENCH_DEFAULT_T10K_IMAGES "binaries/t10k-images-idx3-ubyte"
#define MNIST_BENCH_DEFAULT_T10K_LABELS "binaries/t10k-labels-idx1-ubyte"

typedef enum { MODE_SINGLE = 0, MODE_T10K = 1 } run_mode_t;
typedef enum { MEAS_TOTAL = 0, MEAS_STEADY = 1 } meas_mode_t;

typedef struct mnist_bench_opts_t {
    const char *path_network;
    const char *path_image;
    const char *path_t10k_images;
    const char *path_t10k_labels;

    run_mode_t mode;
    meas_mode_t meas;

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

#endif // MNIST_BENCH_H