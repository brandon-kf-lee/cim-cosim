// mem_controller_registers.h

#ifndef MEM_CONTROLLER_REGISTERS_H
#define MEM_CONTROLLER_REGISTERS_H

#include "neural_network.h"
#include <stdint.h>

// Detect host architecture automatically, but allow manual override
#ifndef HOST_64BIT
    #if UINTPTR_MAX == UINT64_MAX
        #define HOST_64BIT 1  // 64-bit host detected
    #else
        #define HOST_64BIT 0  // 32-bit host detected
    #endif
#endif

// Print detection result
#if HOST_64BIT
    #pragma message("Compiling for 64-bit host (split address registers)")
#else
    #pragma message("Compiling for 32-bit host (single address register)")
#endif

// Memory controller register map offsets
enum : uint32_t {
    // Memory Controller Registers (0x00-0x1F)
    // TODO: Rename CTRL_*
    REG_CONTROL = 0x00, // Control bits (START, READ/WRITE, COMPUTE, IRQ enable)
    REG_ADDR    = 0x04, // Target SRAM address
    REG_LEN     = 0x08, // Transfer size (currently 4 bytes/1 word)
    REG_WDATA   = 0x0C, // Data the CPU wants to write to SRAM
    REG_RDATA   = 0x10, // Data read back from SRAM
    REG_STATUS  = 0x14, // Status bits (BUSY, DONE, ERROR)
};

// CTRL CONTROL bits
static constexpr uint32_t CTRL_START = 1u << 0;   // b0001, 0x01
static constexpr uint32_t CTRL_WRITE = 1u << 1;   // b0010, 0x02
static constexpr uint32_t CTRL_IRQEN = 1u << 3;   // b0100, 0x04
static constexpr uint32_t CTRL_COMPUTE = 1u << 4; // b1000, 0x08

// CTRL STATUS bits // TODO: change to CTRL_*
static constexpr uint32_t STAT_BUSY = 1u << 0;  // b0001, 0x01
static constexpr uint32_t STAT_DONE = 1u << 1;  // b0010, 0x02
static constexpr uint32_t STAT_ERR  = 1u << 2;  // b0100, 0x04

// SRAM Size
static constexpr uint32_t SRAM_SIZE = 4194304; // 4MB

// SRAM Memory Layout (CIM Data Regions)
// SRAM addresses are abstracted away for now, may not represent where the data (weights, input) should be stored in a real CIM system
// Addresses are defined as the start of the region packed right after the previous' size
#define CIM_DATA_REGION  0x00001000   // Marker for the start of CIM data section

#define WEIGHT_BASE_ADDR 0x00001000
#define WEIGHT_SIZE      (NN_OUT_SIZE * NN_IN_SIZE)

#define BIAS_BASE_ADDR   (WEIGHT_BASE_ADDR + WEIGHT_SIZE)           
#define BIAS_SIZE        (NN_OUT_SIZE * sizeof(int32_t))           

#define INPUT_BASE_ADDR  (BIAS_BASE_ADDR + BIAS_SIZE)               
#define INPUT_SIZE       (NN_IN_SIZE)                         

#define OUTPUT_BASE_ADDR (INPUT_BASE_ADDR + INPUT_SIZE)             
#define OUTPUT_SIZE      (NN_OUT_SIZE * sizeof(int32_t))

#define SRAM_COMPUTE_CMD 0xFFFFFFFF  // Special address to signal compute execution


#endif // MEM_CONTROLLER_REGISTERS_H