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
};