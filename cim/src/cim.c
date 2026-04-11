/* cim.c - Compute-In-Memory (CIM) Public API (userspace library) implementation */

#include "cim.h"
#include "cim_internal.h"

/* ---------- Low level helpers ---------- */

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
    if (t->dma_ms <= 0 || t->ctrl_ms <= 0 || t->job_ms <= 0) return CIM_E_INVAL;
    dev->timeouts = *t;
    return CIM_OK;
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
    if (!c.job_irq_path) c.job_irq_path = "/dev/sc_dev_job";
    if (c.dma_mode != CIM_DMA_SINGLE_PHYS) c.dma_mode = CIM_DMA_PAGED;

    if (c.timeouts.dma_ms <= 0)  c.timeouts.dma_ms = 1000;
    if (c.timeouts.ctrl_ms <= 0) c.timeouts.ctrl_ms = 5000;
    if (c.timeouts.job_ms <= 0) c.timeouts.job_ms = 5000;

    cim_dev_t *d = calloc(1, sizeof(*d));
    if (!d) return CIM_E_NOMEM;

    d->pagemap_fd = -1;
    d->devmem_fd = -1;
    d->dma_irq_fd = -1;
    d->ctrl_irq_fd = -1;
    d->job_irq_fd = -1;
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

    d->job_irq_fd = open(c.job_irq_path, O_RDONLY);
    if (d->job_irq_fd < 0) { 
        cim_close(d); 
        return CIM_E_IO; 
    }

    /* Enable IRQ sources at the device-level IRQ mask. */
    mmio_write32(d, REG_IRQ_ENABLE, IRQ_DMA_DONE | IRQ_CTRL_DONE | IRQ_JOB_DONE);

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
    if (d->job_irq_fd >= 0) close(d->job_irq_fd);

    if (d->mmio_map != MAP_FAILED) munmap(d->mmio_map, d->mmio_size);
    if (d->devmem_fd >= 0) close(d->devmem_fd);

    if (d->pagemap_fd >= 0) close(d->pagemap_fd);

    free(d);
}


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

int cim_configure_inference(cim_dev_t *dev,
                            uint64_t input_phys, uint32_t input_len,
                            uint64_t output_phys, uint32_t output_len,
                            uint32_t input_dst_sram, uint32_t output_src_sram)
{
    if (!dev) return CIM_E_INVAL;
    if (!input_phys || !output_phys) return CIM_E_INVAL;
    if (input_len == 0 || output_len == 0) return CIM_E_INVAL;

    /* program job regs */
    mmio_write32(dev, REG_JOB_IN_SRC_LO, (uint32_t)(input_phys & 0xffffffffu));
    mmio_write32(dev, REG_JOB_IN_SRC_HI, (uint32_t)(input_phys >> 32));
    mmio_write32(dev, REG_JOB_IN_DST,    input_dst_sram);
    mmio_write32(dev, REG_JOB_IN_LEN,    input_len);

    mmio_write32(dev, REG_JOB_OUT_SRC,   output_src_sram);
    mmio_write32(dev, REG_JOB_OUT_DST_LO,(uint32_t)(output_phys & 0xffffffffu));
    mmio_write32(dev, REG_JOB_OUT_DST_HI,(uint32_t)(output_phys >> 32));
    mmio_write32(dev, REG_JOB_OUT_LEN,   output_len);

    return CIM_OK;
}

int cim_start_inference(cim_dev_t *dev)
{    
    /* assume registers have been written already; start */
    mmio_write32(dev, REG_JOB_CONTROL, JOB_START);
    return CIM_OK;
}

int cim_wait_inference(cim_dev_t *dev)
{
    if (!dev) return CIM_E_INVAL;
        
    /* wait once */
    return wait_one_irq(dev->job_irq_fd, dev->timeouts.job_ms, "JOB", dev->debug);
}