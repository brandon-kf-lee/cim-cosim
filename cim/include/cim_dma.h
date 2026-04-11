/* cim_dma.h - Compute-In-Memory (CIM) DMA header */

#ifndef CIM_DMA_H
#define CIM_DMA_H

#define _POSIX_C_SOURCE 200809L

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cim_dev cim_dev_t;   // forward decl (keep cim_dev opaque)

/* DMA mode policy (how userspace provides source/dest buffers) */
typedef enum {
    CIM_DMA_PAGED = 0,        /* default: split by page */
    CIM_DMA_SINGLE_PHYS = 1   /* assumes physically contiguous range */
} cim_dma_mode_t;

/* Public DMA helpers */
int cim_dma_write_sram(cim_dev_t *dev, uint32_t dst_sram_addr,
                       const void *src, size_t len);

int cim_dma_read_sram(cim_dev_t *dev, uint32_t src_sram_addr,
                      void *dst, size_t len);

/* Convert an input and output virtual address to physical addresses 
   Simulates the job of an MMU
 */
 int io_virt_to_phys(cim_dev_t *dev,
                     const void *input_virt, const void *output_virt,
                     uint64_t *input_phys, uint64_t *output_phys);

#ifdef __cplusplus
}
#endif

#endif // CIM_DMA_H