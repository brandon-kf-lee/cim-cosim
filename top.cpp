// top.cpp

#include "include/sram.h"
#include "include/mem_controller.h"
#include "include/testbench.h"

int sc_main(int argc, char* argv[]) {
    Sram sram("sram", 1024); // 1KB SRAM
    Mem_Controller mc("mc");
    Testbench tb("tb");


    // Bind testbench socket to memory controller socket
    tb.socket.bind(mc.t_cpu_socket);

    // Bind memory controller socket to SRAM socket
    mc.i_sram_socket.bind(sram.socket);

    sc_core::sc_start();

    return 0;

}