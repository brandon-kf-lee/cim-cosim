// testbench.h

#pragma once

#include <cstdio>
// #include <cstring>
// #include <iostream>

#include "systemc"
#include "tlm"
#include "tlm_utils/simple_initiator_socket.h"

SC_MODULE(Testbench) { 
    // Initiator socket declaration
    tlm_utils::simple_initiator_socket<Testbench> socket;

    // CPU interrupt receiver
    sc_core::sc_in<bool>irq;
    
    void run();

    SC_CTOR(Testbench){
        SC_THREAD(run);
    }
private:
    sc_core::sc_time delay;

    void mmio_write(uint32_t addr_offset, uint32_t value);
    void mmio_read(uint32_t addr_offset, uint32_t& value);

    void load_matrix(float* matrix, int row_size, int col_size, uint32_t base_addr);
    void load_vector(float* vector, int size, uint32_t base_addr);

};