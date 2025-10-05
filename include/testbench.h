// testbench.h

#ifndef TESTBENCH_H
#define TESTBENCH_H

#include <stdio.h>

#include "systemc"
#include "tlm"
#include "tlm_utils/simple_initiator_socket.h"

#include "include/mem_controller_registers.h"
#include "include/mnist_network.h"

SC_MODULE(Testbench) { 
    // Initiator socket declaration
    tlm_utils::simple_initiator_socket<Testbench> socket;

    // CTRL (memory controller) and DMA interrupt handler
    sc_core::sc_in<bool> irq; // TODO: rename ctrl_irq
    sc_core::sc_in<bool> dma_irq;
    
    void run();

    SC_CTOR(Testbench){
        SC_THREAD(run);
    }
private:
    sc_core::sc_time delay;

    // Reading and writing the registers inside the memory controller
    void mmio_write(uint32_t addr_offset, uint32_t value);
    void mmio_read(uint32_t addr_offset, uint32_t& value);
    void ctrl_wait();

    // Abstracted memory controller writes
    void load_matrix(float* matrix, int row_size, int col_size, uint32_t base_addr);
    void load_f_vector(float* vector, int size, uint32_t base_addr);
    void load_u8_vector(uint8_t* vector, int size, uint32_t base_addr);
    
    // DMA functions
    void dma_transfer(const void* src_data, uint32_t size, uint32_t dst_addr);
    void dma_wait();

    void neural_network_softmax(float* activations, int length);
};

#endif // TESTBENCH_H
