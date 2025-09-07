// mem_controller.h
// The CIM device's driver

#pragma once

#include <cstdio>
// #include <cstring>
 #include <iostream>

#include "systemc"
#include "tlm"
#include "tlm_utils/simple_initiator_socket.h"
#include "tlm_utils/simple_target_socket.h"

using namespace sc_core;
using namespace tlm;

SC_MODULE(Mem_Controller) { 
    // CPU (testbench) and SRAM socket declaration
    tlm_utils::simple_target_socket<Mem_Controller> t_cpu_socket;
    tlm_utils::simple_initiator_socket<Mem_Controller> i_sram_socket;

    SC_CTOR(Mem_Controller);

    void b_transport(tlm::tlm_generic_payload &trans, sc_core::sc_time &delay);

    // void run();

    // SC_CTOR(Mem_Controller){
    //     SC_THREAD(run);
    // }
};