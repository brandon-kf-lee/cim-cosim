# Makefile for mem_ctrl

# SystemC include and library locations
SYSTEMC = $(HOME)/systemc
INCLUDE = -I. -I$(SYSTEMC)/include
LIBRARY = $(SYSTEMC)/lib

# Flags
CFLAGS = -std=c++17 $(INCLUDE) 
LDFLAGS = -L$(LIBRARY) -lsystemc -O2 -Wl,-rpath,$(LIBRARY)

CC = g++
RM = rm -f

# Source and object file names
SRC = src/sram.cpp \
	  src/mem_controller.cpp \
	  src/testbench.cpp  \
	  top.cpp
OBJ = sram.o \
	  mem_controller.o \
	  testbench.o
TARGET = sim


all: $(TARGET)


$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LDFLAGS)

test: sim
	./sim

clean:
	$(RM) *.o $(TARGET)