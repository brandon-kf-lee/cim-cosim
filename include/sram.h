// sram.h
// Simulated compute-in-memory memory module

#pragma once

// #include <cstring>
# include <cstdint>
// #include <iostream>

#include "systemc"
#include "tlm"
#include "tlm_utils/simple_target_socket.h"

SC_MODULE(Sram)
{
public: 
    // Target socket declaration
    tlm_utils::simple_target_socket<Sram> socket;
    
    // SRAM module constructor
    Sram(sc_core::sc_module_name name, uint32_t size_bytes);

    // SRAM module destructor
    ~Sram();

    // Blocking transport function
    void b_transport(tlm::tlm_generic_payload &trans, sc_core::sc_time &delay);

private: 
    uint8_t* mem;
    uint32_t mem_size;
    sc_core::sc_time latency;
};