// mem_controller_registers.h

#ifndef MEM_CONTROLLER_REGISTERS_H
#define MEM_CONTROLLER_REGISTERS_H

#include <stdint.h>

// Memory controller register map offsets
enum : uint32_t {
    REG_CONTROL = 0x00, // Control bits (START, READ/WRITE, COMPUTE, IRQ enable)
    REG_ADDR    = 0x04, // Target SRAM address
    REG_LEN     = 0x08, // Transfer size (currently 4 bytes)
    REG_WDATA   = 0x0C, // Data the CPU wants to write to SRAM
    REG_RDATA   = 0x10, // Data read back from SRAM
    REG_STATUS  = 0x14, // Status bits (BUSY, DONE, ERROR)
    REG_SPACE   = 0x20  // Size of MMIO space
};

// CONTROL bits
static constexpr uint32_t CTRL_START = 1u << 0;   // b0001, 0x01
static constexpr uint32_t CTRL_WRITE = 1u << 1;   // b0010, 0x02
static constexpr uint32_t CTRL_IRQEN = 1u << 3;   // b0100, 0x04
static constexpr uint32_t CTRL_COMPUTE = 1u << 4; // b1000, 0x08

// STATUS bits
static constexpr uint32_t STAT_BUSY = 1u << 0;  // b0001, 0x01
static constexpr uint32_t STAT_DONE = 1u << 1;  // b0010, 0x02
static constexpr uint32_t STAT_ERR  = 1u << 2;  // b0100, 0x04

// SRAM Memory Layout - CIM Data Regions
// SRAM addresses are abstracted away for now, may not represent where the data (weights, input) should be stored in a real CIM system
#define WEIGHT_BASE_ADDR 0x00001000  // 0x00001000 - 0x00008FFF  31,360 bytes for uint32_t float weights (10×784)
#define BIAS_BASE_ADDR   0x00009000  // 0x00009000 - 0x00009027  40 bytes for uint32_t float bias (10×1)  
#define INPUT_BASE_ADDR  0x00009028  // 0x00009028 - 0x00009C67  784 bytes for uint8_t input (784×1)
#define OUTPUT_BASE_ADDR 0x00009338  // 0x00009338 - 0x0000935F  40 bytes for uint32_t float output (10×1)
#define SRAM_COMPUTE_CMD 0xFFFFFFFF  // 0xFFFFFFFF               Special address to communicate compute command

#endif // MEM_CONTROLLER_REGISTERS_H