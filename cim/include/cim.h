/* cim.h - Compute-In-Memory (CIM) Public API (userspace library)
 *
 * Design notes:
 * - Uses an opaque device handle (cim_dev_t) to keep state (MMIO mapping, IRQ fds, etc.).
 * - Keeps data structures generic: callers provide their own buffers (weights, inputs, etc.).
 * - Compute is a single "start compute and signal completion"
 * - Timeouts are configured device-wide.
 *
 * Error handling:
 * - Functions return 0 on success, negative error codes on failure.
 * - cim_strerror() converts library error codes to human-readable strings.
 */

#ifndef CIM_H
#define CIM_H

#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cim_dev cim_dev_t;

typedef enum cim_dma_mode {
    CIM_DMA_PAGED = 0,
    CIM_DMA_SINGLE_PHYS = 1,
} cim_dma_mode_t;

typedef struct cim_timeouts {
    int dma_ms;                 /* timeout per DMA transaction/chunk */
    int ctrl_ms;                /* timeout waiting for CTRL completion */
} cim_timeouts_t;

typedef struct cim_config {
    /* MMIO mapping */
    uintptr_t mmio_base;        /* default: 0x8000000 */
    size_t    mmio_size;        /* default: 0x100 */
    const char *devmem_path;    /* default: "/dev/mem" */

    /* IRQ character devices (provided by kernel module) */
    const char *dma_irq_path;   /* default: "/dev/sc_dev_dma" */
    const char *ctrl_irq_path;  /* default: "/dev/sc_dev_ctrl" */

    /* DMA behavior */
    cim_dma_mode_t dma_mode;    /* default: CIM_DMA_PAGED */

    /* Default timeouts (device-wide) */
    cim_timeouts_t timeouts;    /* defaults: dma_ms=1000, ctrl_ms=5000 */

    /* Diagnostics */
    int debug;                  /* default: 0 (off) */
} cim_config_t;

/* Library error codes (negative). */
typedef enum cim_error {
    CIM_OK         = 0,
    CIM_E_INVAL    = -1,  /* invalid argument */
    CIM_E_NOMEM    = -2,  /* allocation failed */
    CIM_E_IO       = -3,  /* I/O error (open/read/write/mmap/poll/etc.) */
    CIM_E_TIMEOUT  = -4,  /* operation timed out */
    CIM_E_PROTO    = -5,  /* unexpected device/library protocol state */
    CIM_E_PERM     = -6,  /* permission error (e.g., /dev/mem) */
} cim_error_t;

/* Convert a cim_error_t to a string (always returns a non-NULL pointer). */
const char *cim_strerror(int err);

/* Initialize the CIM device.
 * - maps MMIO via /dev/mem
 * - opens IRQ fds
 * On success, *out_dev is set and must be released with cim_close().
 */
int cim_init(cim_dev_t **out_dev, const cim_config_t *cfg);
void cim_close(cim_dev_t *dev);

/* Helper: change timeouts after init. */
int cim_set_timeouts(cim_dev_t *dev, const cim_timeouts_t *t);

/* Use DMA to read from device RAM into a userspace buffer
 * src_sram_addr is a byte address in the device SRAM map.
 */
int cim_dma_read_sram(cim_dev_t *dev, uint32_t src_sram_addr, void *dst, size_t len);

/* Use DMA to transfer a userspace buffer into device SRAM.
 * dst_sram_addr is a byte address in the device SRAM map.
 */
int cim_dma_write_sram(cim_dev_t *dev, uint32_t dst_sram_addr,
                       const void *src, size_t len);

/* Start compute-in-memory and wait for completion. */
int cim_compute(cim_dev_t *dev);

/* Read a 32-bit word from device SRAM, using an IRQ to indicate read completion
 *
 * sram_addr: device SRAM byte address (must be 4-byte aligned)
 * out_u32: receives the raw 32-bit value
 */
int cim_read_sram_u32_irq(cim_dev_t *dev,
                          uint32_t sram_addr,
                          uint32_t *out_u32);

/* Read a float stored as IEEE-754 bits in SRAM (IRQ-driven read). */
int cim_read_sram_f32_irq(cim_dev_t *dev,
                          uint32_t sram_addr,
                          float *out_f32);

/* Read the registers that stores the total excess time due to waiting for device & socket overhead. */
int cim_read_correction(cim_dev_t *dev, 
                        int64_t *out_i64);

int cim_clear_correction(cim_dev_t *dev);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif // CIM_H