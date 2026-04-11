/* cim_internal.c - Compute-In-Memory (CIM) internal implementation*/

#include "cim_internal.h"

/*
 * Wait for one IRQ's completion via kernel driver.
 *
 * Note: timeouts are passed in, but are unused in this blocking implementation. 
 * A timer or poll() may be used in addition if desired. 
 *
 * Counting-semantics driver:
 * DMA miscdev (/dev/sc_dev_dma) returns a 32-bit COUNT of DMA_DONE interrupts since the last
 * successful read() from this fd (it returns-and-clears).
 *
 * Memory controller miscdev (/dev/sc_dev_ctrl) returns a 32-bit COUNT of CTRL_DONE interrupts since the last
 * successful read() from this fd (it returns-and-clears).
 */
static int wait_irq_count(int fd, int timeout_ms, uint32_t *out_count)
{
    (void) timeout_ms;  /* unused in blocking mode */
    uint32_t count = 0;
    ssize_t n;

    /*
     * read one event word
     *
     * Counting-semantics driver:
     * read() returns a 32-bit COUNT of interrupts received.
     */
    do {
        n = read(fd, &count, sizeof(count));
    } while (n < 0 && errno == EINTR);

    if (n != (ssize_t)sizeof(count)) return CIM_E_IO;
    if (out_count) *out_count = count;
    return CIM_OK;
}

/* Expect exactly one IRQ occurrence for this operation. */
int wait_one_irq(int fd, int timeout_ms, const char *what, int debug)
{
    (void)timeout_ms; /* unused in blocking mode */

    uint32_t count = 0;
    int rc = wait_irq_count(fd, timeout_ms, &count);
    if (rc != CIM_OK) return rc;

    if (count != 1) {
        if (debug) fprintf(stderr, "cim: unexpected IRQ count=%" PRIu32 " for %s\n",
                           count, what ? what : "op");
        return CIM_E_PROTO;
    }
    return CIM_OK;
}