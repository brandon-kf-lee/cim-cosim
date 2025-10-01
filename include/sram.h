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
    Sram(sc_core::sc_module_name name, uint32_t size_bytes);

    // SRAM module destructor
    ~Sram();

    // Blocking transport function
    void b_transport(tlm::tlm_generic_payload &trans, sc_core::sc_time &delay);

private: 
    uint8_t* mem;
    uint32_t mem_size;
    sc_core::sc_time latency;

    void compute_in_memory();
};

#endif // SRAM_H