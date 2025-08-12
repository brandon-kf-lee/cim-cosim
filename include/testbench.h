// testbench.h

#pragma once

#include <cstdio>
// #include <cstring>
// #include <iostream>

#include "systemc"
#include "tlm"
#include "tlm_utils/simple_initiator_socket.h"

using namespace sc_core;
using namespace tlm;

SC_MODULE(Testbench) { 
    // Initiator socket declaration
    tlm_utils::simple_initiator_socket<Testbench> socket;

    void run();

    SC_CTOR(Testbench){
        SC_THREAD(run);
    }
};