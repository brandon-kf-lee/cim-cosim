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

#define DMA_BUFFER_SIZE (16 * 1024 * 1024)   // 16MB maximum burst size
#define BRIDGE_HEADER_SIZE sizeof(struct bridge_msg)

struct bridge_msg {
    uint8_t  is_write;
    uint8_t  is_dma;
    uint8_t  ctrl_irq; /* IRQ forwarded from SystemC memory controller*/
    uint8_t  reserved; /* Ensure word alignment */

    uint64_t addr;
    uint32_t size;

    uint64_t simulated_ns;
    int8_t   status;
    uint8_t  pad[7];   /* keep alignment/size neat */
} __attribute__((packed));  /* Ensure no padding */

SC_MODULE(Bridge) { 
    // Initiator socket declaration
    tlm_utils::simple_initiator_socket<Bridge> tlm_socket;

    // CTRL (memory controller) and DMA interrupt handler
    sc_core::sc_in<bool> irq; // TODO: rename ctrl_irq
    
    void run();

    SC_CTOR(Bridge){
        SC_THREAD(run);
    }
private:
    sc_core::sc_time delay;

    // Reading and writing the registers inside the memory controller
    tlm::tlm_response_status mmio_write(uint32_t addr_offset, uint32_t value);
    tlm::tlm_response_status mmio_read(uint32_t addr_offset, uint32_t& value);
    tlm::tlm_response_status mmio_write_block(uint32_t addr_offset, uint8_t *value, uint32_t len);
    tlm::tlm_response_status mmio_read_block(uint32_t addr_offset, uint8_t *dst, uint32_t len);
    void ctrl_wait();

    /* Variables to store delay time of each transaction */
    sc_core::sc_time t_start, t_end, t_delta;

};

#endif // BRIDGE_H
