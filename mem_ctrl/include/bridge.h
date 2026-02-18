// bridge.h

#ifndef BRIDGE_H
#define BRIDGE_H


#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>

#include "include/mem_controller_registers.h"

#define DMA_BUFFER_SIZE 4096   // 4KB

struct bridge_msg {
    uint8_t  is_write;
    uint8_t  is_dma;
    uint16_t reserved; /* Ensure word alignment */

    uint64_t addr;
    uint32_t size;

    uint8_t  data[DMA_BUFFER_SIZE];

    int8_t status;
} __attribute__((packed));  /* Ensure no padding */

SC_MODULE(Bridge) { 
    // Initiator socket declaration
    tlm_utils::simple_initiator_socket<Bridge> tlm_socket;

    // CTRL (memory controller) and DMA interrupt handler
    sc_core::sc_in<bool> irq; // TODO: rename ctrl_irq
    sc_core::sc_in<bool> dma_irq;
    
    void run();

    SC_CTOR(Bridge){
        SC_THREAD(run);
    }
private:
    sc_core::sc_time delay;

    // Reading and writing the registers inside the memory controller
    tlm::tlm_response_status mmio_write(uint32_t addr_offset, uint32_t value);
    tlm::tlm_response_status mmio_write_block(uint32_t addr_offset, uint8_t *value, uint32_t len);
    tlm::tlm_response_status mmio_read(uint32_t addr_offset, uint32_t& value);
    void ctrl_wait();

    // // Abstracted memory controller writes
    // void load_matrix(float* matrix, int row_size, int col_size, uint32_t base_addr);
    // void load_f_vector(float* vector, int size, uint32_t base_addr);
    // void load_u8_vector(uint8_t* vector, int size, uint32_t base_addr);
    
    // // DMA functions
    // void dma_transfer(const void* src_data, uint32_t size, uint32_t dst_addr);
    // void dma_wait();

};

#endif // BRIDGE_H
