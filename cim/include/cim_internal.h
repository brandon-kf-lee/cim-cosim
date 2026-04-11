/* cim_internal.h - Compute-In-Memory (CIM) inner workkings
   Includes, register definitions, cim_dev struct, and helper functions
   
   Only implementation files (e.g cim.c, cim_dma.c) should include this file to ensure
   internal headers, etc. are hidden
*/

#ifndef CIM_INTERNAL_H
#define CIM_INTERNAL_H

#define _POSIX_C_SOURCE 200809L

#include "cim.h"
#include "cim_dma.h"
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

/* ---------- Device register definitions ---------- */

/* Memory Controller regs (SystemC side) */
#define REG_CONTROL     0x00        // Control bits (START, READ/WRITE, COMPUTE, IRQ enable)
#define REG_ADDR        0x04        // Target register address
#define REG_LEN         0x08        // Transfer size (currently 4 bytes/1 word)
#define REG_WDATA       0x0C        // Data the CPU wants to write to register
#define REG_RDATA       0x10        // Data read back from register
#define REG_STATUS      0x14        // Status bits (BUSY, DONE, ERROR)

#define CTRL_START      (1u << 0)   // b00001
#define CTRL_WRITE      (1u << 1)   // b00010
#define CTRL_IRQEN      (1u << 3)   // b01000
#define CTRL_COMPUTE    (1u << 4)   // b10000
 
#define STAT_ERR        (1u << 2)   // b00100

/* DMA regs (QEMU-device side) */
#define DMA_CONTROL   0x20          // DMA control
#define DMA_SRC_LO    0x24          // Lower 32-bits of 64-bit address (Guest RAM)
#define DMA_SRC_HI    0x28          // Upper 32-bits of 64-bit address (Guest RAM)
#define DMA_DST       0x2C          // 32-bit address (Device SRAM)
#define DMA_LEN       0x30          // Length of transfer

#define DMA_START     (1u << 0)     // b00001
#define DMA_DIR_READ  (1u << 1)     // b0000x: RAM->SRAM, b0001x: SRAM->RAM 

/* Timing regs */
#define REG_EXCESS_TIME_LO  0x40    // SystemC simulator time correction (upper 32-bits)
#define REG_EXCESS_TIME_HI  0x44    // SystemC simulator time correction (upper 32-bits)

#define REG_TIMING_CLEAR    0x48  // Write to clear timing data
#define TIMING_CLEAR        (1 << 0) 

/* IRQ regs (QEMU-device side) */
#define REG_IRQ_STATUS  0x60        // Read: which IRQs are pending
#define REG_IRQ_CLEAR   0x64        // Write: clear pending IRQs
#define REG_IRQ_ENABLE  0x68        // Enable/disable IRQs

#define IRQ_CTRL_DONE  (1u << 0)    // b001 - bit 0
#define IRQ_DMA_DONE   (1u << 1)    // b010 - bit 1
#define IRQ_JOB_DONE   (1u << 2)    // b100 - bit 2

/* Job registers & bit flags */
#define REG_JOB_CONTROL     0x80    // write 1 to start Job (end-to-end inference)
#define REG_JOB_STATUS      0x84    // DONE/BUSY/ERR

#define REG_JOB_IN_SRC_LO   0x88
#define REG_JOB_IN_SRC_HI   0x8C
#define REG_JOB_IN_DST      0x90
#define REG_JOB_IN_LEN      0x94

#define REG_JOB_OUT_SRC     0x98
#define REG_JOB_OUT_DST_LO  0x9C
#define REG_JOB_OUT_DST_HI  0xA0
#define REG_JOB_OUT_LEN     0xA4

#define JOB_START           (1u << 0)

#define JOB_BUSY            (1u << 0)
#define JOB_DONE            (1u << 1)
#define JOB_ERR             (1u << 2)


/* ---------- Device object ---------- */

struct cim_dev {
    int devmem_fd;
    void *mmio_map;
    volatile uint32_t *regs;

    int dma_irq_fd;
    int ctrl_irq_fd;
    int job_irq_fd;

    cim_dma_mode_t dma_mode;
    cim_timeouts_t timeouts;

    int pagemap_fd;
    size_t mmio_size;
    uintptr_t mmio_base;
    size_t page_sz;

    int debug;
};

/* shared helpers used by cim.c and cim_dma.c */
static inline uint32_t mmio_read32(cim_dev_t *d, uint32_t off) { return d->regs[off/4]; }
static inline void mmio_write32(cim_dev_t *d, uint32_t off, uint32_t v) { d->regs[off/4] = v; }

int wait_one_irq(int fd, int timeout_ms, const char *what, int debug);

#endif //CIM_INTERNAL_H