# train_network
Network generation, training, and quantization pipeline designed to prepare neural network binary models for benchmarking.

## Directory Structure
- `include/`: Header files containing network configurations, dataset layouts, and core structures.
- `src/`: Source files for training, synthetic network generation, file parsing, and quantization.

## Main Source Files
- `mnist_train.c`: Trains a single-layer floating-point neural network on the MNIST dataset.
- `synthetic_train.c`: Creates a 1024x1024 synthetic neural network workload.
- `quantize_network.c`: Quantizes floating-point models into 4-bit representations (`_q4`).
- `neural_network.c`: Implements core neural network math, forward propagation, and weight updates.
- `mnist_file.c`: Parses standard IDX-format MNIST image and label files.


## How to Run

Build and execute workflows using the provided `Makefile`:

* Build everything (MNIST & Synthetic):
  ```bash
    make
  ```

* Train the floating-point MNIST network only:
  ```bash
  make mnist
  ```

* Train and quantize MNIST to 4-bit (`_q4`):
  ```bash
  make mnist_q4
  ```

* Generate the synthetic network workload (already in 4-bit format):
  ```bash
  make synthetic
  ```

* Remove temporary build files:
  ```bash
  make clean
  ```