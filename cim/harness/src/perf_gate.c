#define _GNU_SOURCE

#include "perf_gate.h"

#include <asm/unistd.h>
#include <errno.h>
#include <stdio.h>
#include <linux/perf_event.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <ctype.h>

/*
 * perf_gate: minimal perf_event_open wrapper used by benchmarks to gate
 * performance counters around chosen regions.
 *
 * Design choices:
 *  - Count instructions ONLY (no cycles) to reduce multiplexing/virtualization noise.
 *  - Count both user + kernel instructions. This is important for the CIM datapath
 *    because it includes syscalls (poll/read), driver/IRQ wakeups, and other OS-visible
 *    work. Excluding kernel would underreport real orchestration overhead.
 *  - No inheritance. We count only the calling process, not child tasks.
 *  - No enable_on_exec. The benchmark controls start/stop explicitly.
 *
 * Note:
 *  Under QEMU/SBI PMU virtualization, these counters should be treated as comparative
 *  estimates. They remain useful for apples-to-apples comparisons as long as both
 *  CIM and CPU benchmarks use the same measurement method.
 */

static long perf_event_open_(struct perf_event_attr *hw_event,
                             pid_t pid, int cpu, int group_fd,
                             unsigned long flags)
{
    return syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
}

static int read_u64(int fd, uint64_t *out)
{
    uint64_t v = 0;
    ssize_t n = read(fd, &v, sizeof(v));
    if (n != (ssize_t)sizeof(v)) return -1;
    *out = v;
    return 0;
}

int instret_init()
{
    struct perf_event_attr pe;
    memset(&pe, 0, sizeof(pe));
    pe.type = PERF_TYPE_HARDWARE;
    pe.size = sizeof(struct perf_event_attr);   /* Should be 136 from perf stat -vv */
    pe.config = PERF_COUNT_HW_INSTRUCTIONS;

    pe.disabled = 0;        /* start enabled */
    pe.inherit = 0;         /* do not count child tasks */
    pe.enable_on_exec = 0;  /* do not auto-enable at exec; benchmark controls gating */

    /*
     * IMPORTANT:
     * We intentionally do NOT set exclude_kernel/exclude_user.
     * This counts both user + kernel instructions.
     */

    int ret = (int)perf_event_open_(&pe, 0, -1, -1, PERF_FLAG_FD_CLOEXEC);
    if (ret == -1) {
        perror("perf_event_open instructions");
        return -1;
    }

    return 0;
}

int perf_gate_init(perf_gate_t *pg)
{
    if (!pg) { errno = EINVAL; return -1; }
    memset(pg, 0, sizeof(*pg));
    pg->fd_instr = -1;

    struct perf_event_attr pe;
    memset(&pe, 0, sizeof(pe));
    pe.type = PERF_TYPE_HARDWARE;
    pe.size = sizeof(struct perf_event_attr);   /* Should be 136 from perf stat -vv */
    pe.config = PERF_COUNT_HW_INSTRUCTIONS;

    pe.disabled = 1;        /* start disabled; benchmark enables explicitly */
    pe.inherit = 0;         /* do not count child tasks */
    pe.enable_on_exec = 0;  /* do not auto-enable at exec; benchmark controls gating */

    /*
     * IMPORTANT:
     * We intentionally do NOT set exclude_kernel/exclude_user.
     * This counts both user + kernel instructions.
     */

    int fd_instr = (int)perf_event_open_(&pe, 0, -1, -1, PERF_FLAG_FD_CLOEXEC);
    if (fd_instr == -1) {
        perror("perf_event_open instructions");
        return -1;
    }

    pg->fd_instr = fd_instr;
    return 0;
}

void perf_gate_close(perf_gate_t *pg)
{
    if (!pg) return;
    if (pg->fd_instr != -1) close(pg->fd_instr);
    pg->fd_instr = -1;
}

int perf_gate_reset(perf_gate_t *pg)
{
    if (!pg || pg->fd_instr == -1) { errno = EINVAL; return -1; }
    if (ioctl(pg->fd_instr, PERF_EVENT_IOC_RESET, 0) == -1) return -1;
    return 0;
}

int perf_gate_enable(perf_gate_t *pg)
{
    if (!pg || pg->fd_instr == -1) { errno = EINVAL; return -1; }
    if (ioctl(pg->fd_instr, PERF_EVENT_IOC_ENABLE, 0) == -1) return -1;
    return 0;
}

int perf_gate_reset_enable(perf_gate_t *pg)
{
    if (!pg || pg->fd_instr == -1) { errno = EINVAL; return -1; }
    if (ioctl(pg->fd_instr, PERF_EVENT_IOC_RESET, 0) == -1) return -1;
    if (ioctl(pg->fd_instr, PERF_EVENT_IOC_ENABLE, 0) == -1) return -1;
    return 0;
}

int perf_gate_disable(perf_gate_t *pg)
{
    if (!pg || pg->fd_instr == -1) { errno = EINVAL; return -1; }
    if (ioctl(pg->fd_instr, PERF_EVENT_IOC_DISABLE, 0) == -1) return -1;
    return 0;
}

/*
 * Read the current instruction count.
 *
 * Note: perf counters are monotonic while enabled; if per-region counts is desired,
 * use reset_enable() at the region start, then disable() at the end, then read().
 */
int perf_gate_read(const perf_gate_t *pg, uint64_t *instructions)
{
    if (!pg || pg->fd_instr == -1 || !instructions) {
        errno = EINVAL;
        return -1;
    }
    if (read_u64(pg->fd_instr, instructions) != 0) return -1;
    return 0;
}