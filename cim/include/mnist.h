/* cim_mnist.h - Header file for required structs and memory addresses to run
 * MNIST interence with CIM API
 */

/* ---------- MNIST structs ---------- */

#define MNIST_IMAGE_WIDTH 28
#define MNIST_IMAGE_HEIGHT 28
#define MNIST_IMAGE_SIZE (MNIST_IMAGE_WIDTH * MNIST_IMAGE_HEIGHT)
#define MNIST_LABELS 10

// Holds DNN weights and biases, trained on MNIST images
typedef struct neural_network_t_ {
    float b[MNIST_LABELS];
    float W[MNIST_LABELS][MNIST_IMAGE_SIZE];
} neural_network_t;

// 28 x 28 pixel image with no byte padding
typedef struct mnist_image_t_ {
    uint8_t pixels[MNIST_IMAGE_SIZE];
} __attribute__((packed)) mnist_image_t;

/* ---------- SRAM layout (CIM Data Regions) ---------- */
#define CIM_DATA_REGION  0x00001000
#define WEIGHT_BASE_ADDR 0x00001000  // 0x00001000 - 0x00008FFF (weights)
#define BIAS_BASE_ADDR   0x00009000  // 0x00009000 - 0x00009027 (bias)
#define INPUT_BASE_ADDR  0x00009028  // 0x00009028 - 0x00009C67 (input)
#define OUTPUT_BASE_ADDR 0x00009338  // 0x00009338 - 0x0000935F (output)