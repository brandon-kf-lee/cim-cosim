#define _POSIX_C_SOURCE 200809L

#include "mnist_bench.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int streq(const char *a, const char *b) { return strcmp(a, b) == 0; }

static void usage(const char *argv0)
{
    fprintf(stderr,
        "Usage: %s [options]\n\n"
        "Options:\n"
        "  --mode single|t10k                (default: single)\n"
        "  --iters N                         (default: 1)\n"
        "  --warmup N                        (default: 0)  (not measured)\n"
        "  --measure total|steady            (default: steady)\n"
        "  --network PATH                    (default: %s)\n"
        "  --image PATH                      (default: %s)  [single]\n"
        "  --t10k-images PATH                (default: %s)\n"
        "  --t10k-labels PATH                (default: %s)\n"
        "  --verbose                         (print predictions)\n",
        argv0,
        MNIST_BENCH_DEFAULT_NETWORK,
        MNIST_BENCH_DEFAULT_IMAGE_0,
        MNIST_BENCH_DEFAULT_T10K_IMAGES,
        MNIST_BENCH_DEFAULT_T10K_LABELS);
}

int mnist_bench_parse_args(mnist_bench_opts_t *opts, int argc, char **argv)
{
    if (!opts) return 2;

    opts->path_network    = MNIST_BENCH_DEFAULT_NETWORK;
    opts->path_image      = MNIST_BENCH_DEFAULT_IMAGE_0;
    opts->path_t10k_images= MNIST_BENCH_DEFAULT_T10K_IMAGES;
    opts->path_t10k_labels= MNIST_BENCH_DEFAULT_T10K_LABELS;
    opts->mode = MODE_SINGLE;
    opts->meas = MEAS_STEADY;
    opts->iters = 1;
    opts->warmup = 0;
    opts->verbose = 0;

    for (int i = 1; i < argc; i++) {
        if (streq(argv[i], "--help") || streq(argv[i], "-h")) {
            usage(argv[0]);
            return 2;
        } else if (streq(argv[i], "--mode") && i + 1 < argc) {
            const char *m = argv[++i];
            if (streq(m, "single")) opts->mode = MODE_SINGLE;
            else if (streq(m, "t10k")) opts->mode = MODE_T10K;
            else { fprintf(stderr, "ERROR: unknown mode '%s'\n", m); return 2; }
        } else if (streq(argv[i], "--iters") && i + 1 < argc) {
            opts->iters = strtoull(argv[++i], NULL, 10);
        } else if (streq(argv[i], "--warmup") && i + 1 < argc) {
            opts->warmup = strtoull(argv[++i], NULL, 10);
        } else if (streq(argv[i], "--measure") && i + 1 < argc) {
            const char *m = argv[++i];
            if (streq(m, "total")) opts->meas = MEAS_TOTAL;
            else if (streq(m, "steady")) opts->meas = MEAS_STEADY;
            else { fprintf(stderr, "ERROR: unknown measure '%s'\n", m); return 2; }
        } else if (streq(argv[i], "--network") && i + 1 < argc) {
            opts->path_network = argv[++i];
        } else if (streq(argv[i], "--image") && i + 1 < argc) {
            opts->path_image = argv[++i];
        } else if (streq(argv[i], "--t10k-images") && i + 1 < argc) {
            opts->path_t10k_images = argv[++i];
        } else if (streq(argv[i], "--t10k-labels") && i + 1 < argc) {
            opts->path_t10k_labels = argv[++i];
        } else if (streq(argv[i], "--verbose")) {
            opts->verbose = 1;
        } else {
            fprintf(stderr, "ERROR: unknown/incomplete option '%s'\n", argv[i]);
            return 2;
        }
    }

    if (opts->iters == 0) {
        fprintf(stderr, "ERROR: --iters must be > 0\n");
        return 2;
    }
    return 0;
}

void die_errno(const char *msg)
{
    fprintf(stderr, "ERROR: %s: %s\n", msg, strerror(errno));
    exit(1);
}

int read_exact_file(const char *path, void *dst, size_t size)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "ERROR: failed to open '%s': %s\n", path, strerror(errno));
        return -1;
    }
    size_t n = fread(dst, 1, size, f);
    fclose(f);
    if (n != size) {
        fprintf(stderr, "ERROR: fread('%s') got %zu bytes, expected %zu\n", path, n, size);
        return -1;
    }
    return 0;
}

void neural_network_softmax(float *a, int n)
{
    float max = a[0];
    for (int i = 1; i < n; i++) if (a[i] > max) max = a[i];

    float sum = 0.f;
    for (int i = 0; i < n; i++) { a[i] = expf(a[i] - max); sum += a[i]; }
    for (int i = 0; i < n; i++) a[i] /= sum;
}

int argmax_f32(const float *a, int n)
{
    int idx = 0;
    float maxv = a[0];
    for (int i = 1; i < n; i++) if (a[i] > maxv) { maxv = a[i]; idx = i; }
    return idx;
}

uint8_t pixel_to_u4(uint8_t p)
{
    return (uint8_t)(p >> 4);   // 0..15
}