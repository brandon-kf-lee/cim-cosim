// mem_controller.h
// The CIM device's driver
// Includes DMA access

#ifndef MEM_CONTROLLER_H
#define MEM_CONTROLLER_H

#include <stdio.h>

#include "systemc"
#include "tlm"
#include "tlm_utils/simple_initiator_socket.h"
#include "tlm_utils/simple_target_socket.h"

#include "include/mem_controller_registers.h"
#include "include/timing_params.h"


SC_MODULE(Mem_Controller) { 
    // CPU (testbench) and SRAM socket declaration
    tlm_utils::simple_target_socket<Mem_Controller> t_cpu_socket;
    tlm_utils::simple_initiator_socket<Mem_Controller> i_sram_socket;
    
    // CTRL (memory controller)
    sc_core::sc_out<bool> irq; // TODO: rename ctrl_irq

    SC_CTOR(Mem_Controller);
    ~Mem_Controller();

    void b_transport(tlm::tlm_generic_payload &trans, sc_core::sc_time &delay);

private:
    // Memory controller registers/state
    uint32_t reg_control{0};
    uint32_t reg_addr{0};
    uint32_t reg_len{0};
    uint32_t reg_wdata{0};
    uint32_t reg_rdata{0};
    uint32_t reg_status{0};
    
    // Events
    sc_core::sc_event irq_update_event;     // Trigger irq update //TODO: rename to ctrl_irq_update_event

    // Methods
    void ctrl_handler(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay);
    void dma_handler(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay);
    void start_operation(sc_core::sc_time& delay);
    void prepare_sram_transaction(tlm::tlm_generic_payload& trans, 
                                  tlm::tlm_command cmd, 
                                  uint32_t addr, 
                                  uint8_t* data, 
                                  uint32_t len);

    // Dedicated manager threads to prevent multiple other threads directly accessing irq signal
    void irq_manager();
};

#endif // MEM_CONTROLLER_H