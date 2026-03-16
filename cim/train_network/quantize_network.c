/* quantize_network.c
 *
 * Reads float neural_network_t and writes neural_network_q4_t.
 *
 * Post-Training Quantization (PTQ) scheme :
 *  - activations: unsigned 4-bit x_q in [0,15] with x_scale = 1/15 (so 15 ~= 1.0)
 *  - weights: per-output-channel symmetric signed 4-bit w_q in [-8,7]
 *  - bias: b_q[i] = round( b_float[i] / (x_scale * w_scale[i]) )
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "include/mnist_file.h"
#include "include/neural_network.h"

static int read_exact(const char *path, void *buf, size_t len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    size_t n = fread(buf, 1, len, f);
    fclose(f);
    return (n == len) ? 0 : -1;
}

static int write_exact(const char *path, const void *buf, size_t len)
{
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t n = fwrite(buf, 1, len, f);
    fclose(f);
    return (n == len) ? 0 : -1;
}

static inline int8_t clamp_s4_int(int v)
{
    if (v < -8) return -8;
    if (v >  7) return  7;
    return (int8_t)v;
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: %s mnist_network.bin mnist_network_q4.bin\n", argv[0]);
        return 2;
    }

    const char *in_path  = argv[1];
    const char *out_path = argv[2];

    neural_network_t fnet;
    if (read_exact(in_path, &fnet, sizeof(fnet)) != 0) {
        fprintf(stderr, "error: failed to read %s\n", in_path);
        return 1;
    }

    neural_network_q4_t qnet;
    memset(&qnet, 0, sizeof(qnet));

    // Activation scale for x_q = (pixel >> 4) in [0..15].
    // Interpret 15 as approximately 1.0 (since original CPU path uses pixel/255 in [0,1]).
    qnet.x_scale = 1.0f / 15.0f;

    // Per-output-channel weight quantization + bias quantization
    for (int i = 0; i < MNIST_LABELS; i++) {
        float max_abs = 0.0f;

        for (int j = 0; j < MNIST_IMAGE_SIZE; j++) {
            float a = fabsf(fnet.W[i][j]);
            if (a > max_abs) max_abs = a;
        }
        if (max_abs == 0.0f) max_abs = 1.0f;

        // Map max_abs -> 7 (not 8) for symmetric signed 4-bit
        qnet.w_scale[i] = max_abs / 7.0f;

        // Quantize weights for this output channel
        for (int j = 0; j < MNIST_IMAGE_SIZE; j++) {
            int q = (int)lrintf(fnet.W[i][j] / qnet.w_scale[i]);
            qnet.W[i][j] = clamp_s4_int(q);
        }

        // Bias quantization: b_q in accumulator domain
        const float denom = qnet.x_scale * qnet.w_scale[i];
        qnet.b[i] = (int32_t)lrintf(fnet.b[i] / denom);
    }

    if (write_exact(out_path, &qnet, sizeof(qnet)) != 0) {
        fprintf(stderr, "error: failed to write %s\n", out_path);
        return 1;
    }

    fprintf(stderr, "x_scale=%g\n", qnet.x_scale);
    for (int i = 0; i < MNIST_LABELS; i++) {
        fprintf(stderr, "w_scale[%d]=%g\n", i, qnet.w_scale[i]);
    }
    fprintf(stderr, "wrote %s (%zu bytes)\n", out_path, sizeof(qnet));
    return 0;
}