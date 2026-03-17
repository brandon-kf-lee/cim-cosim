/* cim.c - Compute-In-Memory (CIM) Public API (userspace library) implementation */

#include "cim.h"
#include "cim_registers.h"

/* ---------- Low level helpers ---------- */

static uint32_t mmio_read32(cim_dev_t *d, uint32_t off)
{
    return d->regs[off / 4];
}

static void mmio_write32(cim_dev_t *d, uint32_t off, uint32_t v)
{
    d->regs[off / 4] = v;
}

static int is_perm_error(void)
{
    return (errno == EACCES || errno == EPERM);
}

const char *cim_strerror(int err)
{
    switch (err) {
    case CIM_OK:        return "OK";
    case CIM_E_INVAL:   return "invalid argument";
    case CIM_E_NOMEM:   return "out of memory";
    case CIM_E_IO:      return "I/O error";
    case CIM_E_TIMEOUT: return "timeout";
    case CIM_E_PROTO:   return "protocol/state error";
    case CIM_E_PERM:    return "permission denied";
    default:            return "unknown error";
    }
}

int cim_set_timeouts(cim_dev_t *dev, const cim_timeouts_t *t)
{
    if (!dev || !t) return CIM_E_INVAL;
    if (t->dma_ms <= 0 || t->ctrl_ms <= 0) return CIM_E_INVAL;
    dev->timeouts = *t;
    return CIM_OK;
}

/*
 * Wait for completion via kernel driver.
 *
 * Counting-semantics driver:
 * DMA miscdev (/dev/sc_dev_dma)returns a 32-bit COUNT of DMA_DONE interrupts since the last
 * successful read() from this fd (it returns-and-clears).
 *
 * Memory controller miscdev (/dev/sc_dev_ctrl) returns a 32-bit COUNT of CTRL_DONE interrupts since the last
 * successful read() from this fd (it returns-and-clears).
 */
static int wait_irq_count(int fd, int timeout_ms, uint32_t *out_count)
{
    struct pollfd pfd = {
        .fd = fd,
        .events = POLLIN,
        .revents = 0,
    };

    /*
     * Wait for interrupt until timeout
     *
     * Note: poll() here is not busy-wait polling the device.
     * It sleeps until the kernel driver wakes it (or timeout/signal).
     */
    int r = poll(&pfd, 1, timeout_ms);
    if (r == 0) return CIM_E_TIMEOUT;
    if (r < 0)  return CIM_E_IO;

    /*
     * read one event word
     *
     * Counting-semantics driver:
     * read() returns a 32-bit COUNT of interrupts received.
     */
    uint32_t count = 0;
    ssize_t n = read(fd, &count, sizeof(count));
    if (n != (ssize_t)sizeof(count)) return CIM_E_IO;

    if (out_count) *out_count = count;
    return CIM_OK;
}

/* Expect exactly one IRQ occurrence for this operation. */
static int wait_one_irq(int fd, int timeout_ms, const char *what, int debug)
{
    uint32_t count = 0;
    int rc = wait_irq_count(fd, timeout_ms, &count);
    if (rc != CIM_OK) return rc;

    if (count != 1) {
        if (debug) {
            fprintf(stderr, "cim: unexpected IRQ count=%" PRIu32 " for %s (expected 1)\n",
                    count, what ? what : "op");
        }
        return CIM_E_PROTO;
    }
    return CIM_OK;
}

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
static int dma_transfer_paged(cim_dev_t *d, const void *src, size_t len, uint32_t dst_sram)
{
    if (!d || !src) return CIM_E_INVAL;

    uintptr_t start = (uintptr_t)src;
    size_t off = 0;

    while (off < len) {
        uintptr_t v = start + off;

        /* bytes left in this page from address v */
        size_t in_page = d->page_sz - (v % d->page_sz);
        size_t chunk = len - off;
        if (chunk > in_page) chunk = in_page;
        if (chunk > UINT32_MAX) return CIM_E_INVAL;

        uint64_t phys = virt_to_phys(d, (void *)v);
        if (!phys) {
            if (d->debug) {
                fprintf(stderr, "cim: virt_to_phys failed (v=%p off=%zu)\n", (void *)v, off);
            }
            return CIM_E_IO;
        }

        int rc = dma_transfer_single_phys(d, phys, dst_sram + (uint32_t)off, (uint32_t)chunk);
        if (rc != CIM_OK) return rc;

        off += chunk;
    }

    return CIM_OK;
}

static int dma_read_paged(cim_dev_t *d, uint32_t src_sram, void *dst, size_t len)
{
    if (!d || !dst) return CIM_E_INVAL;

    uintptr_t start = (uintptr_t)dst;
    size_t off = 0;

    while (off < len) {
        uintptr_t v = start + off;

        size_t in_page = d->page_sz - (v % d->page_sz);
        size_t chunk = len - off;
        if (chunk > in_page) chunk = in_page;
        if (chunk > UINT32_MAX) return CIM_E_INVAL;

        uint64_t phys = virt_to_phys(d, (void *)v);
        if (!phys) {
            if (d->debug) {
                fprintf(stderr, "cim: virt_to_phys failed (dst=%p off=%zu)\n", (void *)v, off);
            }
            return CIM_E_IO;
        }

        int rc = dma_read_single_phys(d, src_sram + (uint32_t)off, phys, (uint32_t)chunk);
        if (rc != CIM_OK) return rc;

        off += chunk;
    }

    return CIM_OK;
}

/* Force single chunk transfer by using page-aligned buffers */
static int dma_transfer_single_page(cim_dev_t *d, const void *src, size_t len, uint32_t dst_sram)
{
    uintptr_t start = (uintptr_t)src;
    size_t off_in_page = start % d->page_sz;
    if (off_in_page + len <= d->page_sz && len <= UINT32_MAX) {
        uint64_t phys = virt_to_phys(d, (void *)start);
        if (!phys) return CIM_E_IO;
        return dma_transfer_single_phys(d, phys, dst_sram, (uint32_t)len);
    }
    return dma_transfer_paged(d, src, len, dst_sram);
}

static int dma_read_single_page(cim_dev_t *d, uint32_t src_sram, void *dst, size_t len)
{
    uintptr_t start = (uintptr_t)dst;
    size_t off_in_page = start % d->page_sz;
    if (off_in_page + len <= d->page_sz && len <= UINT32_MAX) {
        uint64_t phys = virt_to_phys(d, (void *)start);
        if (!phys) return CIM_E_IO;
        return dma_read_single_phys(d, src_sram, phys, (uint32_t)len);
    }
    return dma_read_paged(d, src_sram, dst, len);
}


/* ---------- Public API ---------- */

int cim_init(cim_dev_t **out_dev, const cim_config_t *cfg)
{
    if (!out_dev) return CIM_E_INVAL;
    *out_dev = NULL;

    cim_config_t c = {0};
    if (cfg) c = *cfg;

    /* Set default configuration */
    if (c.mmio_base == 0) c.mmio_base = 0x8000000;
    if (c.mmio_size == 0) c.mmio_size = 0x100;
    if (!c.devmem_path) c.devmem_path = "/dev/mem";
    if (!c.dma_irq_path) c.dma_irq_path = "/dev/sc_dev_dma";
    if (!c.ctrl_irq_path) c.ctrl_irq_path = "/dev/sc_dev_ctrl";
    if (c.dma_mode != CIM_DMA_SINGLE_PHYS) c.dma_mode = CIM_DMA_PAGED;

    if (c.timeouts.dma_ms <= 0)  c.timeouts.dma_ms = 1000;
    if (c.timeouts.ctrl_ms <= 0) c.timeouts.ctrl_ms = 5000;

    cim_dev_t *d = calloc(1, sizeof(*d));
    if (!d) return CIM_E_NOMEM;

    d->pagemap_fd = -1;
    d->devmem_fd = -1;
    d->dma_irq_fd = -1;
    d->ctrl_irq_fd = -1;
    d->mmio_map = MAP_FAILED;

    /* Set based on configuration (if provided) */
    d->mmio_base = c.mmio_base;
    d->mmio_size = c.mmio_size;
    d->dma_mode = c.dma_mode;
    d->timeouts = c.timeouts;
    d->debug = c.debug;
    
    /* Declare system page size */
    long ps = sysconf(_SC_PAGESIZE);
    if (ps <= 0) {
        int rc = is_perm_error() ? CIM_E_PERM : CIM_E_IO;
        free(d);
        return rc;
    }
    d->page_sz = (size_t)ps;

    /* Map device registers into memory space */
    d->devmem_fd = open(c.devmem_path, O_RDWR | O_SYNC);
    if (d->devmem_fd < 0) {
        int rc = is_perm_error() ? CIM_E_PERM : CIM_E_IO;
        free(d);
        return rc;
    }

    d->mmio_map = mmap(NULL, d->mmio_size, PROT_READ | PROT_WRITE, MAP_SHARED,
                       d->devmem_fd, d->mmio_base);
    if (d->mmio_map == MAP_FAILED) {
        int rc = is_perm_error() ? CIM_E_PERM : CIM_E_IO;
        close(d->devmem_fd);
        free(d);
        return rc;
    }

    d->regs = (volatile uint32_t *)d->mmio_map;

    /* Open pagemap */
    d->pagemap_fd = open("/proc/self/pagemap", O_RDONLY);
    if (d->pagemap_fd < 0) {
        cim_close(d);
        return CIM_E_IO;
    }

    /* Open kernel IRQ miscdevices */
    d->dma_irq_fd = open(c.dma_irq_path, O_RDONLY);
    if (d->dma_irq_fd < 0) {
        cim_close(d);
        return CIM_E_IO;
    }

    d->ctrl_irq_fd = open(c.ctrl_irq_path, O_RDONLY);
    if (d->ctrl_irq_fd < 0) {
        cim_close(d);
        return CIM_E_IO;
    }

    /* Enable both sources at the device-level IRQ mask. */
    mmio_write32(d, REG_IRQ_ENABLE, IRQ_DMA_DONE | IRQ_CTRL_DONE);

    *out_dev = d;
    return CIM_OK;
}

void cim_close(cim_dev_t *d)
{
    if (!d) return;

    if (d->regs && d->mmio_map != MAP_FAILED) {
        /* Best-effort disable device IRQ generation */
        mmio_write32(d, REG_IRQ_ENABLE, 0);
    }

    if (d->ctrl_irq_fd >= 0) close(d->ctrl_irq_fd);
    if (d->dma_irq_fd >= 0) close(d->dma_irq_fd);

    if (d->mmio_map != MAP_FAILED) munmap(d->mmio_map, d->mmio_size);
    if (d->devmem_fd >= 0) close(d->devmem_fd);

    if (d->pagemap_fd >= 0) close(d->pagemap_fd);

    free(d);
}

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

    return dma_read_single_page(dev, src_sram_addr, dst, len);
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
        if (len > UINT32_MAX) return CIM_E_INVAL;
        return dma_transfer_single_phys(dev, phys, dst_sram_addr, (uint32_t)len);
    }

    return dma_transfer_single_page(dev, src, len, dst_sram_addr);}

int cim_compute(cim_dev_t *dev)
{
    if (!dev) return CIM_E_INVAL;

    /* Program compute start (IRQ enabled) */
    mmio_write32(dev, REG_LEN, 4);
    mmio_write32(dev, REG_CONTROL, CTRL_IRQEN | CTRL_COMPUTE | CTRL_START);

    return wait_one_irq(dev->ctrl_irq_fd, dev->timeouts.ctrl_ms, "compute", dev->debug);
}

int cim_read_sram_u32_irq(cim_dev_t *dev, uint32_t sram_addr, uint32_t *out_u32)
{
    if (!dev || !out_u32) return CIM_E_INVAL;
    if (sram_addr & 3u) return CIM_E_INVAL;

    /* Start controller read with IRQ enabled */
    mmio_write32(dev, REG_ADDR, sram_addr);
    mmio_write32(dev, REG_LEN, 4);
    mmio_write32(dev, REG_CONTROL, CTRL_IRQEN | CTRL_START);

    int rc = wait_one_irq(dev->ctrl_irq_fd, dev->timeouts.ctrl_ms, "ctrl read", dev->debug);
    if (rc != CIM_OK) return rc;

    uint32_t st = mmio_read32(dev, REG_STATUS);
    if (st & STAT_ERR) {
        /* Operation completed but controller flagged error */
        return CIM_E_PROTO;
    }

    *out_u32 = mmio_read32(dev, REG_RDATA);
    return CIM_OK;
}

int cim_read_sram_f32_irq(cim_dev_t *dev, uint32_t sram_addr, float *out_f32)
{
    if (!dev || !out_f32) return CIM_E_INVAL;

    uint32_t u = 0;
    int rc = cim_read_sram_u32_irq(dev, sram_addr, &u);
    if (rc != CIM_OK) return rc;

    memcpy(out_f32, &u, sizeof(*out_f32));
    return CIM_OK;
}

int cim_read_correction(cim_dev_t *dev, int64_t *out_i64) {
        
    if (!dev || !out_i64) return CIM_E_INVAL;

    int64_t correction_lo = (int64_t)mmio_read32(dev, REG_EXCESS_TIME_LO);
    int64_t correction_hi = (int64_t)mmio_read32(dev, REG_EXCESS_TIME_HI);
    *out_i64 = (correction_hi << 32) | (correction_lo & 0xffffffffLL);
    
    return CIM_OK;
}

int cim_clear_correction(cim_dev_t *dev) {

    if (!dev) return CIM_E_INVAL;
    mmio_write32(dev, REG_TIMING_CLEAR, TIMING_CLEAR);
    return CIM_OK;

}
