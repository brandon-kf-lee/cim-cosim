/* cim_registers.h - Compute-In-Memory (CIM) includes, register definitions and structs */

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

/* Timing regs */
#define REG_EXCESS_TIME_LO  0x40    // SystemC simulator time correction (upper 32-bits)
#define REG_EXCESS_TIME_HI  0x44    // SystemC simulator time correction (upper 32-bits)

/* IRQ regs (QEMU-device side) */
#define REG_IRQ_STATUS  0x60        // Read: which IRQs are pending
#define REG_IRQ_CLEAR   0x64        // Write: clear pending IRQs
#define REG_IRQ_ENABLE  0x68        // Enable/disable IRQs

#define IRQ_CTRL_DONE  (1u << 0)    // b01 - bit 0
#define IRQ_DMA_DONE   (1u << 1)    // b10 - bit 1

/* ---------- Device object ---------- */

struct cim_dev {
    int devmem_fd;
    void *mmio_map;
    volatile uint32_t *regs;

    int dma_irq_fd;
    int ctrl_irq_fd;

    cim_dma_mode_t dma_mode;
    cim_timeouts_t timeouts;

    size_t mmio_size;
    uintptr_t mmio_base;
    size_t page_sz;

    int debug;
};