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

#define _POSIX_C_SOURCE 200809L

/* Pull in DMA API */
#include "cim_dma.h"

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cim_dev cim_dev_t;

typedef struct cim_timeouts {
    int dma_ms;                 /* timeout per DMA transaction/chunk */
    int ctrl_ms;                /* timeout waiting for CTRL completion */
    int job_ms;                 /* timeout waiting for end-to-end inference completion */
} cim_timeouts_t;

typedef struct cim_config {
    /* MMIO mapping */
    uintptr_t mmio_base;        /* default: 0x8000000 */
    size_t    mmio_size;        /* default: 0x100 */
    const char *devmem_path;    /* default: "/dev/mem" */

    /* IRQ character devices (provided by kernel module) */
    const char *dma_irq_path;   /* default: "/dev/sc_dev_dma" */
    const char *ctrl_irq_path;  /* default: "/dev/sc_dev_ctrl" */
    const char *job_irq_path;   /* default: "/dev/sc_dev_job" */

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

/* Read the registers that stores the total excess time due to waiting for device & socket overhead. */
int cim_read_correction(cim_dev_t *dev, 
                        int64_t *out_i64);

int cim_clear_correction(cim_dev_t *dev);

/* Program Job registers with input & output registers */
int cim_configure_inference(cim_dev_t *dev,
                            uint64_t input_phys, uint32_t input_len,
                            uint64_t output_phys, uint32_t output_len,
                            uint32_t input_dst_sram, uint32_t output_src_sram);

/* Start execution 
 * Assumed cim_configure_inference() was already ran
 */
int cim_start_inference(cim_dev_t *dev);

/* Wait for one IRQ from Job's completion */
int cim_wait_inference(cim_dev_t *dev);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif // CIM_H