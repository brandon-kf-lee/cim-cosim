// top.cpp

#include "include/sram.h"
#include "include/testbench.h"

int sc_main(int argc, char* argv[]) {
    Sram new_sram("new_sram", 1024); // 1KB SRAM
    Testbench tb("tb");

    // Bind SRAM socket to testbench socket
    tb.socket.bind(new_sram.socket);

    sc_core::sc_start();

    return 0;

}