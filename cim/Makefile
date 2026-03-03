# Makefile

CC      := gcc
CFLAGS  := -std=c11 -Wall -Wextra -O2
INCLUDES = -Iinclude
LDFLAGS :=

BUILD_DIR := build
SRC_DIR   := src
EX_DIR    := examples

LIB_OBJS := $(BUILD_DIR)/cim.o

.PHONY: all clean run mnist_demo

all: mnist_demo

# ----- Library objects -----
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/cim.o: $(SRC_DIR)/cim.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# ----- Example: mnist_demo -----
mnist_demo: $(BUILD_DIR)/mnist_demo

$(BUILD_DIR)/mnist_demo: $(EX_DIR)/mnist_demo.c $(LIB_OBJS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) $^ -o $@ $(LDFLAGS)

clean:
	rm -rf $(BUILD_DIR)