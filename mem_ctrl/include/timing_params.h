// timing_params.h

#ifndef TIMING_PARAMS_H
#define TIMING_PARAMS_H
#include "systemc"

namespace timing {

    // --------------- SRAM ---------------
    /* SRAM port latency (abstract)
     * Model one SRAM read/write port access as 1 cycle at the modeled device clock.
     * The device clock is set to 100 MHz (10 ns) to match the CIM macro operating point.
     */
    static const sc_core::sc_time SRAM_RW = sc_core::sc_time(10, sc_core::SC_NS);

    // --------------- Control/MMIO ---------------
    /* Control-register (MMIO) access latency (abstract)
     * We model a single control-register read/write as a fixed-latency transaction.
     * This is a simplified proxy for a peripheral bus access (e.g., AXI4-Lite/APB)
     *
     * Note: can be set to multiple cycles (e.g., 20–50 ns) for more conservative modeling.
     */
    static const sc_core::sc_time CTRL_REG = sc_core::sc_time(10, sc_core::SC_NS);


    // --------------- DMA ---------------
    /* DMA timing model (hardware-side):
     *   T_dma = T_fixed + (bytes / BW)
     *
     * Notes:
     *  - DMA register programming and paged-DMA software overhead occur in QEMU/guest,
     *    not in SystemC, and are therefore excluded here.
     *  - Parameter values are chosen conservatively but grounded in vendor IP numbers.
     *
     * Citations:
     *  - Xilinx LogiCORE AXI DMA v7.1 Product Guide (PG021, Oct 5 2016):
     *      * Reports DMA latency components in clock cycles (Table 2-2).
     *      * Reports sustained throughput on a 10,000B transfer at 100 MHz:
     *          MM2S ≈ 399 MB/s, S2MM ≈ 299 MB/s (Table 2-3).
     *
     *  Reasoning:
     *  - Use BW = 200 MB/s (0.2 bytes/ns), which is below the ~299–399 MB/s reported in PG021
     *    to account for non-idealities and to avoid overestimating DMA capability.
     *  - Use T_fixed = 500 ns to represent a small but non-zero per-transfer overhead
     *    (descriptor/control pipeline/arbitration), consistent with PG021’s cycle-level
     *    latency magnitudes (Table 2-2) when expressed at 100 MHz.
     */
    static const uint64_t DMA_FIXED_NS = 500;            // ns
    static constexpr double DMA_BYTES_PER_NS = 0.2;      // 200 MB/s = 0.2 bytes/ns

    inline sc_core::sc_time dma_transfer_time(uint32_t bytes) {
        const double transfer_ns = (double)bytes / DMA_BYTES_PER_NS;
        const double total_ns = (double)DMA_FIXED_NS + transfer_ns;
        return sc_core::sc_time(total_ns, sc_core::SC_NS);
    }


    // --------------- CIM ---------------
    /* CIM compute timing model
     * Derived from You et al. (2024) digital SRAM CIM macro architecture:
     *  - 64x64 macro => tile width of 64
     *  - multi-bit MAC: a set of MAC ops is completed every M clock cycles 
     *    on average for every M-bit input width
     *  - for 4b inputs: 4 cycles/tile
     *  - 100 MHz operating point => 10 ns/cycle
     */
    static const uint64_t TILE_W = 64;
    static const uint64_t INPUT_BITS = 4;
    static const uint64_t CYCLES_PER_TILE = INPUT_BITS;
    static const uint64_t CYCLE_NS = 10;

    inline sc_core::sc_time cim_time(int tiles) {
        const uint64_t total_ns =
            (uint64_t)tiles * (uint64_t)CYCLES_PER_TILE * (uint64_t)CYCLE_NS;
        return sc_core::sc_time(total_ns, sc_core::SC_NS);
    }

} // namespace timing

#endif // TIMING_PARAMS_H