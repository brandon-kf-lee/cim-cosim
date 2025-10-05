// mem_controller_registers.h

#ifndef MEM_CONTROLLER_REGISTERS_H
#define MEM_CONTROLLER_REGISTERS_H

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
    
    // DMA Registers (0x20-0x3F)
#if HOST_64BIT
    // 64-bit host: Need split address registers
    DMA_CONTROL     = 0x20, // DMA control bits (START, IRQEN)
    DMA_SRC_ADDR_LO = 0x24, // DMA source address - low 32 bits
    DMA_SRC_ADDR_HI = 0x28, // DMA source address - high 32 bits  
    DMA_DST_ADDR    = 0x2C, // DMA destination address (32-bit SRAM)
    DMA_LENGTH      = 0x30, // DMA transfer length in bytes
    DMA_STATUS      = 0x34, // DMA status register
#else
    // 32-bit host: Single address register
    DMA_CONTROL  = 0x20,    // DMA control bits (START, IRQEN)
    DMA_SRC_ADDR = 0x24,    // DMA source address (32-bit)
    DMA_DST_ADDR = 0x28,    // DMA destination address (32-bit SRAM)
    DMA_LENGTH   = 0x2C,    // DMA transfer length in bytes
    DMA_STATUS   = 0x30,    // DMA status register
#endif
    
    REG_SPACE    = 0x40  // Total MMIO register space size
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

// DMA CONTROL bits
static constexpr uint32_t DMA_START  = 1u << 0;  // 0x01
static constexpr uint32_t DMA_IRQEN = 1u << 1;  // 0x02

// DMA STATUS bits
static constexpr uint32_t DMA_BUSY = 1u << 0;   // 0x01
static constexpr uint32_t DMA_DONE = 1u << 1;   // 0x02
static constexpr uint32_t DMA_ERR  = 1u << 2;   // 0x04

// SRAM Size
static constexpr uint32_t SRAM_SIZE = 262144; // 256KB SRAM

// SRAM Memory Layout (CIM Data Regions)
// SRAM addresses are abstracted away for now, may not represent where the data (weights, input) should be stored in a real CIM system
#define WEIGHT_BASE_ADDR 0x00001000  // 0x00001000 - 0x00008FFF  31,360 bytes for uint32_t float weights (10×784)
#define BIAS_BASE_ADDR   0x00009000  // 0x00009000 - 0x00009027  40 bytes for uint32_t float bias (10×1)  
#define INPUT_BASE_ADDR  0x00009028  // 0x00009028 - 0x00009C67  784 bytes for uint8_t input (784×1)
#define OUTPUT_BASE_ADDR 0x00009338  // 0x00009338 - 0x0000935F  40 bytes for uint32_t float output (10×1)
#define SRAM_COMPUTE_CMD 0xFFFFFFFF  // 0xFFFFFFFF               Special address to signal compute execution

#endif // MEM_CONTROLLER_REGISTERS_H