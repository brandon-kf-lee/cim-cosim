/* mnist_demo.c - Example program using the CIM userspace library
 *
 * Workflow
 * - read mnist_network.bin and MNIST_0.bin from the guest filesystem
 * - DMA weights/bias/input into CIM SRAM (library uses paged DMA workaround)
 * - start compute (IRQ-driven)
 * - read back 10 output floats using IRQ-driven reads
 * - softmax + argmax
 *
 */

#include "cim.h"
#include "cim_mnist.h"

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Defaults (if no CLI args provided) */
#define DEFAULT_PATH_NETWORK   "binaries/mnist_network.bin"
#define DEFAULT_PATH_IMAGE_0   "binaries/MNIST_0.bin"

/* Read exactly size bytes into dst from a .bin file. Returns 0 on success. */
static int read_exact_file(const char *path, void *dst, size_t size)
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

static void usage(const char *argv0)
{
    fprintf(stderr,
            "Usage:\n"
            "  %s [mnist_network.bin] [mnist_image.bin]\n\n"
            "Defaults:\n"
            "  network: %s\n"
            "  image:   %s\n",
            argv0, DEFAULT_PATH_NETWORK, DEFAULT_PATH_IMAGE_0);
}

/* Numerically-stable softmax */
static void neural_network_softmax(float *activations, int length)
{
    int i;
    float sum, max;

    for (i = 1, max = activations[0]; i < length; i++) {
        if (activations[i] > max) {
            max = activations[i];
        }
    }

    for (i = 0, sum = 0; i < length; i++) {
        activations[i] = expf(activations[i] - max);
        sum += activations[i];
    }

    for (i = 0; i < length; i++) {
        activations[i] /= sum;
    }
}

int main(int argc, char **argv)
{
    int rc;
    cim_dev_t *dev = NULL;

    neural_network_t network;
    mnist_image_t image;

    const char *path_network = DEFAULT_PATH_NETWORK;
    const char *path_image   = DEFAULT_PATH_IMAGE_0;

    if (argc > 3) {
        usage(argv[0]);
        return 2;
    }
    if (argc >= 2) path_network = argv[1];
    if (argc >= 3) path_image   = argv[2];

    printf("CIM MNIST Demo\n\n");
    printf("Network file: %s\n", path_network);
    printf("Image file:   %s\n\n", path_image);

    /* Load the pre-trained network */
    if (read_exact_file(path_network, &network, sizeof(network)) != 0) {
        return 1;
    }

    /* Load pre-processed MNIST image */
    if (read_exact_file(path_image, &image, sizeof(image)) != 0) {
        return 1;
    }

    cim_config_t cfg = {0};
    cfg.dma_mode = CIM_DMA_PAGED;      /* safe default (multi-page buffers) */
    cfg.timeouts.dma_ms = 1000;
    cfg.timeouts.ctrl_ms = 5000;
    cfg.debug = 0;                     /* set to 1 for verbose DMA/IRQ messages */

    rc = cim_init(&dev, &cfg);
    if (rc != CIM_OK) {
        fprintf(stderr, "cim_init failed: %s (%d)\n", cim_strerror(rc), rc);
        return 1;
    }

    printf("Loading weights/bias/input into SRAM using DMA\n");

    rc = cim_dma_write_sram(dev, WEIGHT_BASE_ADDR, network.W, sizeof(network.W));
    if (rc != CIM_OK) {
        fprintf(stderr, "DMA weights failed: %s (%d)\n", cim_strerror(rc), rc);
        cim_close(dev);
        return 1;
    }

    rc = cim_dma_write_sram(dev, BIAS_BASE_ADDR, network.b, sizeof(network.b));
    if (rc != CIM_OK) {
        fprintf(stderr, "DMA bias failed: %s (%d)\n", cim_strerror(rc), rc);
        cim_close(dev);
        return 1;
    }

    rc = cim_dma_write_sram(dev, INPUT_BASE_ADDR, image.pixels, sizeof(image.pixels));
    if (rc != CIM_OK) {
        fprintf(stderr, "DMA image failed: %s (%d)\n", cim_strerror(rc), rc);
        cim_close(dev);
        return 1;
    }

    printf("CPU: All DMA transfers complete, starting compute operation...\n");

    rc = cim_compute(dev);
    if (rc != CIM_OK) {
        fprintf(stderr, "cim_compute failed: %s (%d)\n", cim_strerror(rc), rc);
        cim_close(dev);
        return 1;
    }

    printf("CPU: Compute operation completed, reading results...\n");

    float activations[MNIST_LABELS] = {0};

    printf("CPU: Reading %d activation values...\n", MNIST_LABELS);
    for (int i = 0; i < MNIST_LABELS; ++i) {
        uint32_t addr = OUTPUT_BASE_ADDR + (uint32_t)(i * sizeof(float));
        rc = cim_read_sram_f32_irq(dev, addr, &activations[i]);
        if (rc != CIM_OK) {
            fprintf(stderr, "read activation[%d] failed: %s (%d)\n", i, cim_strerror(rc), rc);
            cim_close(dev);
            return 1;
        }
    }

    neural_network_softmax(activations, MNIST_LABELS);

    int prediction = 0;
    float max_activation = activations[0];

    for (int i = 0; i < MNIST_LABELS; ++i) {
        printf("%d: %f\n", i, activations[i]);
        if (activations[i] > max_activation) {
            max_activation = activations[i];
            prediction = i;
        }
    }

    printf("Predicted number: %d\n", prediction);

    cim_close(dev);
    return 0;
}