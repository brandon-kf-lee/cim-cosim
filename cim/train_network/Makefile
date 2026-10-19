CC = gcc
CFLAGS = -Wall -Wextra -Iinclude -lm

all: mnist synthetic

# Train floating-point MNIST network
mnist: 
    @mkdir -p ../binaries
    $(CC) $(CFLAGS) src/mnist_train.c src/mnist_file.c src/neural_network.c -o mnist_train
    ./mnist_train
    @rm mnist_train

# Train floating-point MNIST network & quantize
mnist_q4: mnist
    $(CC) $(CFLAGS) src/quantize_network.c src/neural_network.c -o quantize_mnist
    ./quantize_mnist ../binaries/mnist_network.bin ../binaries/mnist_network_q4.bin
    @rm quantize_mnist

# Generate synthetic network
synthetic: 
    @mkdir -p ../binaries
    $(CC) $(CFLAGS) src/synthetic_train.c src/neural_network.c -o synthetic_train
    ./synthetic_train
    @rm synthetic_train

clean:
    @rm -f mnist_train synthetic_train quantize_mnist quantize_synthetic