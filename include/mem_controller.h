// mem_controller.h
// The CIM device's driver

#pragma once

#include <cstdio>
// #include <cstring>
#include <iostream>

#include "mem_controller_registers.h"
#include "systemc"
#include "tlm"
#include "tlm_utils/simple_initiator_socket.h"
#include "tlm_utils/simple_target_socket.h"

SC_MODULE(Mem_Controller) { 
    // CPU (testbench) and SRAM socket declaration
    tlm_utils::simple_target_socket<Mem_Controller> t_cpu_socket;
    tlm_utils::simple_initiator_socket<Mem_Controller> i_sram_socket;
    
    // CPU interrupt sender
    sc_core::sc_out<bool> irq;

    SC_CTOR(Mem_Controller);

    void b_transport(tlm::tlm_generic_payload &trans, sc_core::sc_time &delay);

private:
    // Internal registers/state
    uint32_t reg_control{0};
    uint32_t reg_addr{0};
    uint32_t reg_len{0};
    uint32_t reg_wdata{0};
    uint32_t reg_rdata{0};
    uint32_t reg_status{0};

    void start_operation(sc_core::sc_time& delay);
    void update_irq();

};