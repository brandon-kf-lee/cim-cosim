# Makefile for mem_ctrl

# Architecture detection and override options
ARCH_64BIT ?= auto

# Network selection option (0 = MNIST, 1 = Large Synthetic)
SYNTHETIC ?= 0

# SystemC include and library locations
SYSTEMC = $(SYSTEMC_HOME)
INCLUDE = -I. -I$(SYSTEMC)/include
LIBRARY = $(SYSTEMC)/lib-linux64

# Base flags
CFLAGS = -std=c++17 $(INCLUDE) -g -O0

# Set architecture flag based on ARCH_64BIT variable
ifeq ($(ARCH_64BIT),force32)
    CFLAGS += -DDMA_HOST_64BIT=0
    $(info Building for 32-bit host (forced))
else ifeq ($(ARCH_64BIT),force64)
    CFLAGS += -DDMA_HOST_64BIT=1
    $(info Building for 64-bit host (forced))
else
    $(info Building with automatic architecture detection)
endif

# Set network flag based on SYNTHETIC variable
ifeq ($(SYNTHETIC),1)
    CFLAGS += -DUSE_SYNTHETIC_NETWORK=1
    $(info *** Building for Synthetic Network ***)
else
    $(info *** Building for Original MNIST Network (784x10) ***)
endif

LDFLAGS = -L$(LIBRARY) -lsystemc -Wl,-rpath,$(LIBRARY)

CC = g++
RM = rm -f

# Source and object file names
SRC = src/sram.cpp \
	  src/mem_controller.cpp \
	  src/bridge.cpp  \
	  top.cpp
OBJ = sram.o \
	  mem_controller.o \
	  bridge.o
TARGET = listen

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LDFLAGS)

test: listen
	./listen

clean:
	$(RM) *.o $(TARGET)

# Help target explaining architecture and network options
help:
	@echo "Usage:"
	@echo "  make                    - Build for MNIST with auto architecture detection"
	@echo "  make SYNTHETIC=1        - Build for Synthetic Network"
	@echo "  make ARCH_64BIT=force32 - Force 32-bit host mode (single DMA address register)"
	@echo "  make ARCH_64BIT=force64 - Force 64-bit host mode (split DMA address registers)"
	@echo "  make test               - Build and run bridge"
	@echo "  make clean              - Remove build artifacts"
	@echo "  make help               - Show this help message"

.PHONY: all test clean help