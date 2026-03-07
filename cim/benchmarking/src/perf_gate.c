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

static long perf_event_open_(struct perf_event_attr *hw_event,
                             pid_t pid, int cpu, int group_fd, unsigned long flags)
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

int perf_gate_init(perf_gate_t *pg)
{
    if (!pg) { errno = EINVAL; return -1; }
    memset(pg, 0, sizeof(*pg));
    pg->fd_leader = pg->fd_instr = pg->fd_cycles = -1;

    struct perf_event_attr pe;
    memset(&pe, 0, sizeof(pe));
    pe.type = PERF_TYPE_HARDWARE;
    pe.size = 136; /* from: perf stat -vv */
    pe.config = PERF_COUNT_HW_INSTRUCTIONS;
    pe.disabled = 1;
    pe.inherit = 1;
    pe.enable_on_exec = 1;

    // fprintf(stderr, "userspace sizeof(perf_event_attr)=%zu, pe.size=%u\n",
    //         sizeof(struct perf_event_attr), pe.size);

    int fd_instr = (int)perf_event_open_(&pe, 0, -1, -1, PERF_FLAG_FD_CLOEXEC);
    if (fd_instr == -1) { perror("perf_event_open instructions"); return -1; }

    struct perf_event_attr ce;
    memset(&ce, 0, sizeof(ce));
    ce.type = PERF_TYPE_HARDWARE;
    ce.size = 136; /* from: perf stat -vv */
    ce.config = PERF_COUNT_HW_CPU_CYCLES;
    ce.disabled = 1;
    ce.inherit = 1;
    ce.enable_on_exec = 1;

    int fd_cycles = (int)perf_event_open_(&ce, 0, -1, fd_instr, PERF_FLAG_FD_CLOEXEC);
    if (fd_cycles == -1) { perror("perf_event_open cycles"); close(fd_instr); return -1; }

    pg->fd_leader = fd_instr;
    pg->fd_instr  = fd_instr;
    pg->fd_cycles = fd_cycles;
    return 0;
}

void perf_gate_close(perf_gate_t *pg)
{
    if (!pg) return;
    if (pg->fd_cycles != -1) close(pg->fd_cycles);
    if (pg->fd_instr  != -1) close(pg->fd_instr);
    pg->fd_leader = -1;
    pg->fd_instr  = -1;
    pg->fd_cycles = -1;
}

int perf_gate_enable(perf_gate_t *pg)
{
    if (!pg || pg->fd_leader == -1) { errno = EINVAL; return -1; }
    if (ioctl(pg->fd_leader, PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP) == -1) return -1;
    return 0;
}


int perf_gate_reset_enable(perf_gate_t *pg)
{
    if (!pg || pg->fd_leader == -1) { errno = EINVAL; return -1; }
    if (ioctl(pg->fd_leader, PERF_EVENT_IOC_RESET, PERF_IOC_FLAG_GROUP) == -1) return -1;
    if (ioctl(pg->fd_leader, PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP) == -1) return -1;
    return 0;
}

int perf_gate_disable(perf_gate_t *pg)
{
    if (!pg || pg->fd_leader == -1) { errno = EINVAL; return -1; }
    if (ioctl(pg->fd_leader, PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP) == -1) return -1;
    return 0;
}

int perf_gate_read(const perf_gate_t *pg, uint64_t *instructions, uint64_t *cycles)
{
    if (!pg || pg->fd_instr == -1 || pg->fd_cycles == -1 || !instructions || !cycles) {
        errno = EINVAL;
        return -1;
    }
    if (read_u64(pg->fd_instr, instructions) != 0) return -1;
    if (read_u64(pg->fd_cycles, cycles) != 0) return -1;
    return 0;
}