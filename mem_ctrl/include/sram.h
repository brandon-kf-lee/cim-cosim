// sram.h
// Simulated compute-in-memory memory module

#ifndef SRAM_H
#define SRAM_H

#include "systemc"
#include "tlm"
#include "tlm_utils/simple_target_socket.h"

#include "include/mem_controller_registers.h"
#include "include/mnist_network.h"

SC_MODULE(Sram)
{
public: 
    // Target socket declaration
    tlm_utils::simple_target_socket<Sram> socket;
    
    // SRAM module constructor
    Sram(sc_core::sc_module_name name);

    // SRAM module destructor
    ~Sram();

    // Blocking transport function
    void b_transport(tlm::tlm_generic_payload &trans, sc_core::sc_time &delay);

private:
    uint8_t* mem;

    // Base latency for ordinary SRAM reads/writes (your existing simple model).
    sc_core::sc_time mem_latency;

    // ---- CIM timing knobs (derived from You et al. 2024) ----
    // Model the macro as a 64-wide tile engine, matching the 64x64 CIM array dimension
    static constexpr int TILE_W = 64;

    // For multi-bit MAC, a set of MAC ops is completed every M clock cycles on average, 
    // where M is the input bitwidth.
    // For 4b inputs => ~4 cycles per tile.
    static constexpr int INPUT_BITS = 4;
    static constexpr int CYCLES_PER_TILE = INPUT_BITS;

    // Modeled on 4b/4b MAC operation at 100 MHz
    // 100 MHz => 10 ns per cycle.
    static constexpr uint32_t CYCLE_NS = 10;

    void compute_in_memory(sc_core::sc_time &delay);
};

#endif // SRAM_H