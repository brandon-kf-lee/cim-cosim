# Top-level Makefile (cim/)

CC       := gcc
CFLAGS   := -std=c11 -Wall -Wextra -O2
LDFLAGS  :=

BUILD_DIR := build
BIN_DIR   := binaries

# Main project dirs (TODO: wire these into build targets later)
SRC_DIR   := src
EX_DIR    := examples
INC_DIR   := include
HAR_DIR   := harness

# Training/quantization toolchain (from mnist-DNN-C repo)
TN_DIR    := train_network
TN_INC    := -I$(TN_DIR)/include

# Main project include path (not for use with TN tools)
PROJ_INC  := -I$(INC_DIR)

.PHONY: all clean models train quantize_q4 clean_models synthetic \
        # TODO targets:
        # lib examples harness

all: models
	@echo "TODO: build lib/examples/harness here (deploy script currently handles these)."

# ------------------------------------------------------------
# Directories
# ------------------------------------------------------------
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

# ------------------------------------------------------------
# Training tools (compiled only against train_network/include)
# ------------------------------------------------------------

TN_COMMON_SRCS := \
	$(TN_DIR)/mnist_file.c \
	$(TN_DIR)/neural_network.c

$(BUILD_DIR)/mnist_train: $(TN_DIR)/mnist_train.c $(TN_COMMON_SRCS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(TN_INC) $^ -o $@ -lm

$(BUILD_DIR)/quantize_network_q4: $(TN_DIR)/quantize_network.c $(TN_COMMON_SRCS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(TN_INC) $^ -o $@ -lm

$(BUILD_DIR)/synthetic_train: $(TN_DIR)/synthetic_train.c $(TN_COMMON_SRCS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(TN_INC) $^ -o $@ -lm		

# Run training to produce float model
# Assumes mnist_train.c writes: binaries/mnist_network.bin
train: $(BUILD_DIR)/mnist_train | $(BIN_DIR)
	$(BUILD_DIR)/mnist_train

# Run quantizer to produce 4-bit model
# Assumes quantize_network.c usage: quantize_network <in> <out>
quantize_q4: $(BUILD_DIR)/quantize_network_q4 | $(BIN_DIR)
	$(BUILD_DIR)/quantize_network_q4 $(BIN_DIR)/mnist_network.bin $(BIN_DIR)/mnist_network_q4.bin

# Create synthetic model
synthetic: $(BUILD_DIR)/synthetic_train | $(BIN_DIR)
	$(BUILD_DIR)/synthetic_train

models: train quantize_q4 synthetic

# ------------------------------------------------------------
# TODO: integrate these into Makefile (currently in deploy script)
# ------------------------------------------------------------
# lib:
#	$(CC) ... $(SRC_DIR)/cim.c ... -> build library objects
#
# harness:
#	$(CC) ... $(HAR_DIR)/src/... -> perf_gate, mnist_bench objects
#
# examples:
#	$(CC) ... $(EX_DIR)/mnist_CIM.c ... link against lib + harness
#	$(CC) ... $(EX_DIR)/mnist_CPU.c ... link against harness
#
# Consider producing:
#   build/mnist_CIM, build/mnist_CPU
#   and a "make bench" target to run examples/run_bench.sh

# ------------------------------------------------------------
# Cleanup
# ------------------------------------------------------------
clean:
	rm -rf $(BUILD_DIR)

clean_models:
	rm -f $(BIN_DIR)/mnist_network.bin $(BIN_DIR)/mnist_network_q4.bin