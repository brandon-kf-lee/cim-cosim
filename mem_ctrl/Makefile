# Makefile for mem_ctrl

# Architecture detection and override options
ARCH_64BIT ?= auto

# SystemC include and library locations
SYSTEMC = $(SYSTEMC_HOME)
INCLUDE = -I. -I$(SYSTEMC)/include
LIBRARY = $(SYSTEMC)/lib

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

# Help target explaining architecture options
help:
	@echo "Usage:"
	@echo "  make                    - Build with automatic architecture detection"
	@echo "  make ARCH_64BIT=force32 - Force 32-bit host mode (single DMA address register)"
	@echo "  make ARCH_64BIT=force64 - Force 64-bit host mode (split DMA address registers)"
	@echo "  make test               - Build and run bridge"
	@echo "  make clean              - Remove build artifacts"
	@echo "  make help               - Show this help message"

.PHONY: all test clean help