// top.cpp

#include "include/sram.h"
#include "include/mem_controller.h"
#include "include/mem_controller_registers.h"
#include "include/testbench.h"

int sc_main(int argc, char* argv[]) {
    Sram sram("sram");
    Mem_Controller mc("mc");
    Testbench tb("tb");

    // Sockets
    tb.socket.bind(mc.t_cpu_socket);    // Bind testbench socket to memory controller socket
    mc.i_sram_socket.bind(sram.socket); // Bind memory controller socket to SRAM socket

    // IRQ wiring
    sc_core::sc_signal<bool> ctrl_irq_sig("ctrl_irq_sig");
    sc_core::sc_signal<bool> dma_irq_sig("dma_irq_sig");
    
    // Mem controller (CTRL) interrupts
    // TODO: Rename irq to ctrl_irq
    mc.irq(ctrl_irq_sig);
    tb.irq(ctrl_irq_sig);
    
    // DMA interrupts
    mc.dma_irq(dma_irq_sig);
    tb.dma_irq(dma_irq_sig);

    sc_core::sc_start();

    return 0;

}