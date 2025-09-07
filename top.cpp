// top.cpp

#include "include/sram.h"
#include "include/mem_controller.h"
#include "include/testbench.h"

int sc_main(int argc, char* argv[]) {
    Sram sram("sram", 1024); // 1KB SRAM
    Mem_Controller mc("mc");
    Testbench tb("tb");

    // Sockets
    tb.socket.bind(mc.t_cpu_socket);    // Bind testbench socket to memory controller socket
    mc.i_sram_socket.bind(sram.socket); // Bind memory controller socket to SRAM socket

    // IRQ wiring
    sc_core::sc_signal<bool> irq_sig("irq_sig");
    mc.irq(irq_sig);
    tb.irq(irq_sig);

    sc_core::sc_start();

    return 0;

}