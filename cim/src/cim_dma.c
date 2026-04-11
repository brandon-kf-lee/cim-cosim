/* cim_dma.c - Compute-In-Memory (CIM) DMA implementation 
 * This is the CPU preparing and setting DMA registers
 */

#include "cim_dma.h"
#include "cim_internal.h"

/* Get physical address of a virtual address (ONE PAGE ONLY).
 * This is a best-effort helper for the paged DMA workaround.
 */
static uint64_t virt_to_phys(cim_dev_t *d, void *vaddr)
{
    uint64_t page = 0;
    size_t pagesz = d->page_sz;

    off_t offset = (off_t)(((uintptr_t)vaddr / pagesz) * sizeof(uint64_t));

    if (pread(d->pagemap_fd, &page, sizeof(page), offset) != (ssize_t)sizeof(page)) {
        if (d->debug) perror("pread pagemap");
        return 0;
    }

    if (!(page & (1ULL << 63))) {
        if (d->debug) fprintf(stderr, "Page not present in memory\n");
        return 0;
    }

	/* Physical page frame number is in bits 0-54 */
    uint64_t phys = (page & ((1ULL << 55) - 1)) * pagesz;
    phys += (uintptr_t)vaddr % pagesz;
    return phys;
}

/* Single contiguous DMA transaction (assumes src_phys..src_phys+len is contiguous). */
static int dma_transfer_single_phys(cim_dev_t *d, uint64_t src_phys, uint32_t dst_sram, uint32_t len)
{
    if (!d || !d->regs) return CIM_E_INVAL;
    if (len == 0) return CIM_E_INVAL;

    if (d->debug) {
        fprintf(stderr, "cim: DMA src_phys=0x%016" PRIx64 " dst=0x%08x len=%u\n",
                src_phys, dst_sram, len);
    }

    /* Set source address (64-bit host address, split into LO/HI) */
    mmio_write32(d, DMA_SRC_LO, (uint32_t)(src_phys & 0xffffffffu));
    mmio_write32(d, DMA_SRC_HI, (uint32_t)(src_phys >> 32));

    /* Set destination address (32-bit address in SRAM) */
    mmio_write32(d, DMA_DST, dst_sram);

    /* Set transfer length */
    mmio_write32(d, DMA_LEN, len);

    /* Start DMA */
    mmio_write32(d, DMA_CONTROL, DMA_START);

    return wait_one_irq(d->dma_irq_fd, d->timeouts.dma_ms, "DMA", d->debug);
}

static int dma_read_single_phys(cim_dev_t *d, uint32_t src_sram, uint64_t dst_phys, uint32_t len)
{
    if (!d || !d->regs) return CIM_E_INVAL;
    if (len == 0) return CIM_E_INVAL;

    if (d->debug) {
        fprintf(stderr, "cim: DMA READ src_sram=0x%08x dst_phys=0x%016" PRIx64 " len=%u\n",
                src_sram, dst_phys, len);
    }

    /* Keep old meaning:
     *  DMA_SRC = guest RAM (here: destination phys)
     *  DMA_DST = device SRAM (here: source sram addr)
     */
    mmio_write32(d, DMA_SRC_LO, (uint32_t)(dst_phys & 0xffffffffu));
    mmio_write32(d, DMA_SRC_HI, (uint32_t)(dst_phys >> 32));
    mmio_write32(d, DMA_DST, src_sram);
    mmio_write32(d, DMA_LEN, len);

    /* Start DMA read (DEV->RAM) */
    mmio_write32(d, DMA_CONTROL, DMA_START | DMA_DIR_READ);

    return wait_one_irq(d->dma_irq_fd, d->timeouts.dma_ms, "DMA read", d->debug);
}

/* Paged DMA workaround:
 * Split transfer so that no DMA crosses a page boundary in the source buffer.
 */


 /*
 * Use DMA to transfer a multi-page userspace buffer safely by sending one page at a time.
 *
 * This works around the fact that normal userspace memory is not physically
 * contiguous across pages. It assumes each single page is contiguous.
 *
 * Requirements:
 * - 'src' does NOT need to be page-aligned  (head/tail is handled), but be
 *   careful to never DMA across a page boundary.
 */

static int dma_transfer_paged(cim_dev_t *d, const void *src, uint32_t dst_sram, size_t len)
{
    if (!d || !src) return CIM_E_INVAL;

    uintptr_t start_vaddr = (uintptr_t)src;
    uintptr_t page_start = start_vaddr & ~(d->page_sz - 1); // Align down to page boundary
    size_t offset_in_first_page = start_vaddr - page_start;
    
    // Calculate total pages spanning this buffer
    size_t num_pages = (offset_in_first_page + len + d->page_sz - 1) / d->page_sz;

    // Allocate buffer for bulk pagemap read
    uint64_t *pagemap = malloc(num_pages * sizeof(uint64_t));
    if (!pagemap) return CIM_E_NOMEM;

    // BULK READ: Read all page entries in one syscall
    off_t file_offset = (page_start / d->page_sz) * sizeof(uint64_t);
    if (pread(d->pagemap_fd, pagemap, num_pages * sizeof(uint64_t), file_offset) != (ssize_t)(num_pages * sizeof(uint64_t))) {
        if (d->debug) perror("cim: bulk pread pagemap failed");
        free(pagemap);
        return CIM_E_IO;
    }

    int rc = CIM_OK;
    size_t bytes_remaining = len;
    uintptr_t current_vaddr = start_vaddr;
    uint32_t current_sram = dst_sram;
    size_t page_idx = 0;

    while (bytes_remaining > 0) {
        // Ensure page is present in memory
        if (!(pagemap[page_idx] & (1ULL << 63))) {
            if (d->debug) fprintf(stderr, "cim: page not present during DMA read\n");
            rc = CIM_E_IO;
            break;
        }

        // Get starting physical address of this chunk
        uint64_t chunk_phys = (pagemap[page_idx] & ((1ULL << 55) - 1)) * d->page_sz;
        chunk_phys += (current_vaddr % d->page_sz);
        
        size_t chunk_len = d->page_sz - (current_vaddr % d->page_sz);
        if (chunk_len > bytes_remaining) chunk_len = bytes_remaining;

        // COALESCE LOOP: Look ahead to see if the NEXT pages are physically contiguous
        while (chunk_len < bytes_remaining) {
            uint64_t next_page_entry = pagemap[page_idx + 1];
            
            // If next page isn't present, or isn't physically contiguous, stop coalescing
            if (!(next_page_entry & (1ULL << 63))) break;
            
            uint64_t expected_phys = (pagemap[page_idx] & ((1ULL << 55) - 1)) + 1;
            uint64_t actual_phys = (next_page_entry & ((1ULL << 55) - 1));
            
            if (expected_phys != actual_phys) break; // Memory is fragmented here

            // Next page is contiguous, add it to this DMA transfer
            size_t add_len = d->page_sz;
            if (chunk_len + add_len > bytes_remaining) {
                add_len = bytes_remaining - chunk_len;
            }
            
            // Hardware length limitation check
            if ((chunk_len + add_len) > UINT32_MAX) break; 

            chunk_len += add_len;
            page_idx++;
        }

        // Do one hardware DMA transfer for this whole contiguous chunk
        rc = dma_transfer_single_phys(d, chunk_phys, current_sram, (uint32_t)chunk_len);
        if (rc != CIM_OK) break;

        bytes_remaining -= chunk_len;
        current_vaddr += chunk_len;
        current_sram += chunk_len;
        page_idx++;
    }

    free(pagemap);
    return rc;
} 

static int dma_read_paged(cim_dev_t *d, uint32_t src_sram, void *dst, size_t len)
{
    if (!d || !dst) return CIM_E_INVAL;

    uintptr_t start_vaddr = (uintptr_t)dst;
    uintptr_t page_start = start_vaddr & ~(d->page_sz - 1); // Align down to page boundary
    size_t offset_in_first_page = start_vaddr - page_start;
    
    // Calculate total pages spanning this buffer
    size_t num_pages = (offset_in_first_page + len + d->page_sz - 1) / d->page_sz;

    // Allocate buffer for bulk pagemap read
    uint64_t *pagemap = malloc(num_pages * sizeof(uint64_t));
    if (!pagemap) return CIM_E_NOMEM;

    // BULK READ: Read all page entries in one syscall
    off_t file_offset = (page_start / d->page_sz) * sizeof(uint64_t);
    if (pread(d->pagemap_fd, pagemap, num_pages * sizeof(uint64_t), file_offset) != (ssize_t)(num_pages * sizeof(uint64_t))) {
        if (d->debug) perror("cim: bulk pread pagemap failed");
        free(pagemap);
        return CIM_E_IO;
    }

    int rc = CIM_OK;
    size_t bytes_remaining = len;
    uintptr_t current_vaddr = start_vaddr;
    uint32_t current_sram = src_sram;
    size_t page_idx = 0;

    while (bytes_remaining > 0) {
        // Ensure page is present in memory
        if (!(pagemap[page_idx] & (1ULL << 63))) {
            if (d->debug) fprintf(stderr, "cim: page not present during DMA read\n");
            rc = CIM_E_IO;
            break;
        }

        // Get starting physical address of this chunk
        uint64_t chunk_phys = (pagemap[page_idx] & ((1ULL << 55) - 1)) * d->page_sz;
        chunk_phys += (current_vaddr % d->page_sz);
        
        size_t chunk_len = d->page_sz - (current_vaddr % d->page_sz);
        if (chunk_len > bytes_remaining) chunk_len = bytes_remaining;

        // COALESCE LOOP: Look ahead to see if the NEXT pages are physically contiguous
        while (chunk_len < bytes_remaining) {
            uint64_t next_page_entry = pagemap[page_idx + 1];
            
            // If next page isn't present, or isn't physically contiguous, stop coalescing
            if (!(next_page_entry & (1ULL << 63))) break;
            
            uint64_t expected_phys = (pagemap[page_idx] & ((1ULL << 55) - 1)) + 1;
            uint64_t actual_phys = (next_page_entry & ((1ULL << 55) - 1));
            
            if (expected_phys != actual_phys) break; // Memory is fragmented here

            // Next page is contiguous, add it to this DMA transfer
            size_t add_len = d->page_sz;
            if (chunk_len + add_len > bytes_remaining) {
                add_len = bytes_remaining - chunk_len;
            }
            
            // Hardware length limitation check
            if ((chunk_len + add_len) > UINT32_MAX) break; 

            chunk_len += add_len;
            page_idx++;
        }

        // Do one hardware DMA transfer for this whole contiguous chunk
        rc = dma_read_single_phys(d, current_sram, chunk_phys, (uint32_t)chunk_len);
        if (rc != CIM_OK) break;

        bytes_remaining -= chunk_len;
        current_vaddr += chunk_len;
        current_sram += chunk_len;
        page_idx++;
    }

    free(pagemap);
    return rc;
}

/* ---------- Public API ---------- */

int cim_dma_read_sram(cim_dev_t *dev, uint32_t src_sram_addr, void *dst, size_t len)
{
    if (!dev || !dst) return CIM_E_INVAL;
    if (len == 0) return CIM_OK;

    if (dev->dma_mode == CIM_DMA_SINGLE_PHYS) {
        uint64_t phys = virt_to_phys(dev, dst);
        if (!phys) return CIM_E_IO;
        if (len > UINT32_MAX) return CIM_E_INVAL;
        return dma_read_single_phys(dev, src_sram_addr, phys, (uint32_t)len);
    }

    return dma_read_paged(dev, src_sram_addr, dst, len);
}

int cim_dma_write_sram(cim_dev_t *dev, uint32_t dst_sram_addr,
                       const void *src, size_t len)
{
    if (!dev || !src) return CIM_E_INVAL;
    if (len == 0) return CIM_OK;

    /*
     * NOTE:
     * - CIM_DMA_PAGED is the safe default for normal userspace buffers.
     * - CIM_DMA_SINGLE_PHYS is only safe when the DMA range is physically contiguous.
     */
    if (dev->dma_mode == CIM_DMA_SINGLE_PHYS) {
        uint64_t phys = virt_to_phys(dev, (void *)src);
        if (!phys) return CIM_E_IO;
        return dma_transfer_single_phys(dev, phys, dst_sram_addr, (uint32_t)len);
    }

    return dma_transfer_paged(dev, src, dst_sram_addr, len);
}

int io_virt_to_phys(cim_dev_t *dev,
                    const void *input_virt, const void *output_virt,
                    uint64_t *input_phys, uint64_t *output_phys)
{
    if (!dev || !input_virt || !output_virt) return CIM_E_INVAL;

    *input_phys  = virt_to_phys(dev, (void *)input_virt);
    *output_phys = virt_to_phys(dev, (void *)output_virt);
    if (!*input_phys || !*output_phys) return CIM_E_IO;
    return CIM_OK;
}