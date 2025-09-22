// mem_controller_registers.h

#pragma once
#include <cstdint>

// Memory controller register map offsets
enum : uint32_t {
    REG_CONTROL = 0x00, // Control bits (START, READ/WRITE, IRQ enable)
    REG_ADDR    = 0x04, // Target SRAM address
    REG_LEN     = 0x08, // Transfer size (currently 4 bytes)
    REG_WDATA   = 0x0C, // Data the CPU wants to write to SRAM
    REG_RDATA   = 0x10, // Data read back from SRAM
    REG_STATUS  = 0x14, // Status bits (BUSY, DONE, ERROR)
    REG_SPACE   = 0x20  // Size of MMIO space
};

// CONTROL bits
// TODO: create ctrl start computation bit
static constexpr uint32_t CTRL_START = 1u << 0; // b0001, 0x01
static constexpr uint32_t CTRL_WRITE = 1u << 1; // b0010, 0x02
static constexpr uint32_t CTRL_IRQEN = 1u << 2; // b0100, 0x04

// STATUS bits
static constexpr uint32_t STAT_BUSY = 1u << 0;  // b0001, 0x01
static constexpr uint32_t STAT_DONE = 1u << 1;  // b0010, 0x02
static constexpr uint32_t STAT_ERR  = 1u << 2;  // b0100, 0x04

// SRAM Memory Layout - CIM Data Regions
// SRAM addresses are abstracted away for now, may not represent where the data (weights, input) should be stored in a real CIM system
#define WEIGHT_BASE_ADDR 0x00001000  // 0x00001000 - 0x00008FFF  31,360 bytes for float weights (10×784)
#define BIAS_BASE_ADDR   0x00009000  // 0x00009000 - 0x00009027  40 bytes for bias (10×1)  
#define INPUT_BASE_ADDR  0x00009028  // 0x00009028 - 0x00009C67  3,136 bytes for input (784×1)
#define OUTPUT_BASE_ADDR 0x00009C68  // 0x00009C68 - 0x00009C8F  40 bytes for output (10×1)
