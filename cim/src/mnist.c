#include "mnist.h"

static uint32_t be32(const uint8_t b[4])
{
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | (uint32_t)b[3];
}

/* ---------- IDX dataset loader ---------- */
int load_t10k_dataset(const char *images_path, const char *labels_path, mnist_dataset_t *out)
{
    memset(out, 0, sizeof(*out));

    FILE *fi = fopen(images_path, "rb");
    if (!fi) {
        fprintf(stderr, "ERROR: open('%s'): %s\n", images_path, strerror(errno));
        return -1;
    }

    uint8_t hdr[16];
    if (fread(hdr, 1, sizeof(hdr), fi) != sizeof(hdr)) {
        fprintf(stderr, "ERROR: short read header '%s'\n", images_path);
        fclose(fi);
        return -1;
    }

    uint32_t magic = be32(&hdr[0]);
    uint32_t count = be32(&hdr[4]);
    uint32_t rows  = be32(&hdr[8]);
    uint32_t cols  = be32(&hdr[12]);

    if (magic != 2051) {
        fprintf(stderr, "ERROR: images IDX magic %u (expected 2051)\n", magic);
        fclose(fi);
        return -1;
    }
    if (rows * cols != MNIST_IMAGE_SIZE) {
        fprintf(stderr, "ERROR: images are %ux%u, expected 28x28\n", rows, cols);
        fclose(fi);
        return -1;
    }

    mnist_image_t *images = (mnist_image_t *)malloc((size_t)count * sizeof(mnist_image_t));
    if (!images) {
        fprintf(stderr, "ERROR: malloc images failed\n");
        fclose(fi);
        return -1;
    }

    size_t img_bytes = (size_t)count * (size_t)MNIST_IMAGE_SIZE;
    if (fread(images, 1, img_bytes, fi) != img_bytes) {
        fprintf(stderr, "ERROR: short read images '%s'\n", images_path);
        free(images);
        fclose(fi);
        return -1;
    }
    fclose(fi);

    FILE *fl = fopen(labels_path, "rb");
    if (!fl) {
        fprintf(stderr, "ERROR: open('%s'): %s\n", labels_path, strerror(errno));
        free(images);
        return -1;
    }

    uint8_t lhdr[8];
    if (fread(lhdr, 1, sizeof(lhdr), fl) != sizeof(lhdr)) {
        fprintf(stderr, "ERROR: short read label header '%s'\n", labels_path);
        free(images);
        fclose(fl);
        return -1;
    }

    uint32_t lmagic = be32(&lhdr[0]);
    uint32_t lcount = be32(&lhdr[4]);
    if (lmagic != 2049) {
        fprintf(stderr, "ERROR: labels IDX magic %u (expected 2049)\n", lmagic);
        free(images);
        fclose(fl);
        return -1;
    }
    if (lcount != count) {
        fprintf(stderr, "ERROR: labels count %u != images count %u\n", lcount, count);
        free(images);
        fclose(fl);
        return -1;
    }

    uint8_t *labels = (uint8_t *)malloc(lcount);
    if (!labels) {
        fprintf(stderr, "ERROR: malloc labels failed\n");
        free(images);
        fclose(fl);
        return -1;
    }

    if (fread(labels, 1, lcount, fl) != lcount) {
        fprintf(stderr, "ERROR: short read labels '%s'\n", labels_path);
        free(labels);
        free(images);
        fclose(fl);
        return -1;
    }
    fclose(fl);

    out->images = images;
    out->labels = labels;
    out->size   = count;
    return 0;
}

void free_dataset(mnist_dataset_t *d)
{
    if (!d) return;
    free(d->images);
    free(d->labels);
    d->images = NULL;
    d->labels = NULL;
    d->size = 0;
}