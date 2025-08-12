// sram.h

#pragma once

// #include <cstring>
// #include <iostream>

#include "systemc"
#include "tlm"
#include "tlm_utils/simple_target_socket.h"

using namespace sc_core;
using namespace tlm;

SC_MODULE(Sram)
{
public: 
    // Target socket declaration
    tlm_utils::simple_target_socket<Sram> socket;
    
    // SRAM module constructor
    Sram(sc_module_name name, uint32_t size_bytes);

    // Blocking transport function
    void b_transport(tlm_generic_payload &trans, sc_time &delay);

private: 
    uint8_t* mem;
    uint32_t mem_size;
    sc_time latency;
};