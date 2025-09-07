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
static constexpr uint32_t CTRL_START = 1u << 0; // b0001
static constexpr uint32_t CTRL_WRITE = 1u << 1; // b0010
static constexpr uint32_t CTRL_IRQEN = 1u << 2; // b0100

// STATUS bits
static constexpr uint32_t STAT_BUSY = 1u << 0;  // b0001
static constexpr uint32_t STAT_DONE = 1u << 1;  // b0010
static constexpr uint32_t STAT_ERR  = 1u << 2;  // b0100
