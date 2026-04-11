#ifndef PERF_GATE_H
#define PERF_GATE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct perf_gate {
    int fd_leader;   /* group leader */
    int fd_instr;    /* PERF_COUNT_HW_INSTRUCTIONS */
} perf_gate_t;

/* Initialize perf counters (disabled). Returns 0 on success, -1 on error (errno set). */
int perf_gate_init(perf_gate_t *pg);

/* Close fds and clear struct. Safe to call on partially-initialized pg. */
void perf_gate_close(perf_gate_t *pg);

/* Reset counters to 0 */
int perf_gate_reset(perf_gate_t *pg);

/* Enable without resetting */
int perf_gate_enable(perf_gate_t *pg);  

/* Reset counters to 0 and enable counting (group). Returns 0 on success, -1 on error. */
int perf_gate_reset_enable(perf_gate_t *pg);

/* Disable counting (group). Returns 0 on success, -1 on error. */
int perf_gate_disable(perf_gate_t *pg);

/* Read counters (does not enable/disable). Returns 0 on success, -1 on error. */
int perf_gate_read(const perf_gate_t *pg, uint64_t *instructions);

#ifdef __cplusplus
}
#endif

#endif // PERF_GATE_H