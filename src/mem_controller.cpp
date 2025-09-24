// mem_controller.cpp

#include "include/mem_controller.h"

using namespace sc_core;
using namespace tlm;

// Memory controller constructor, target for CPU module, initiating for SRAM module
Mem_Controller::Mem_Controller(sc_module_name name): 
    sc_module(name), 
    t_cpu_socket("t_cpu_socket"), 
    i_sram_socket("i_sram_socket"), 
    irq("irq"){
    
    t_cpu_socket.register_b_transport(this, &Mem_Controller::b_transport);
    irq.initialize(false);
}

// Update interrupt based on status of the operation (through reg_status)
void Mem_Controller::update_irq() {
    bool enable = (reg_control & CTRL_IRQEN) != 0; // Are IRQs enabled?
    bool done   = (reg_status & STAT_DONE) != 0;   // Is the operation done?
    irq.write(enable && done);                     // Raise IRQ if both is true
}

/* 
    Perform one memory operation using the parameters in the controller’s registers
    CPU writes to registers to access memory:
        CONTROL, ADDR, LEN, WDATA.
    CPU reads from registers to get data & check status:
        RDATA, STATUS
    CPU signals computation in SRAM by setting SRAM_COMPUTE_CMD:
        ADDR
    Controller sends a bus transaction into SRAM, SRAM executes it.
    Controller updates STATUS: BUSY=0, DONE=1. Raises IRQ if enabled.
    CPU either polls DONE or gets interrupted, then reads back RDATA (for reads).
*/
void Mem_Controller::start_operation(sc_core::sc_time& delay) {
    // Basic check, enforces 32-bit accesses
    if (reg_len != 4) {
        reg_status |= STAT_ERR;
        update_irq();
        return;
    }

    // Set busy status before transaction: set BUSY=1, DONE=0.
    reg_status |= STAT_BUSY;
    reg_status &= ~STAT_DONE;
    update_irq();

    // Prepare TLM transaction to SRAM
    tlm::tlm_generic_payload trans;
    uint8_t buf[4]; // Temporary data buffer
    trans.set_address(static_cast<sc_dt::uint64>(reg_addr)); // Address from reg_addr
    trans.set_data_length(4);                                // 4 bytes data length
    trans.set_streaming_width(4);                            // Also 4 (no wraparound)
    trans.set_byte_enable_ptr(nullptr);                      // None, allows all bytes
    trans.set_dmi_allowed(false);                            // Disable DMI
    trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE); // Transaction not complete yet, wait for read/write

    // COMPUTE, send special compute command through address
    if(reg_control & CTRL_COMPUTE) { 
    
        trans.set_command(tlm::TLM_WRITE_COMMAND);
        trans.set_address(SRAM_COMPUTE_CMD); // Tell SRAM to start compute, not read/write from memory
        trans.set_data_length(0);
        trans.set_data_ptr(nullptr);
        i_sram_socket->b_transport(trans, delay);

    // WRITE, copy reg_wdata to buffer, send and write to sram through b_transport
    } else if (reg_control & CTRL_WRITE) {
        std::memcpy(buf, &reg_wdata, 4);
        trans.set_command(tlm::TLM_WRITE_COMMAND);
        trans.set_data_ptr(buf);
        i_sram_socket->b_transport(trans, delay);    
    
    // READ, read from buffer into reg_rdata
    } else {
        trans.set_command(tlm::TLM_READ_COMMAND);
        trans.set_data_ptr(buf);
        i_sram_socket->b_transport(trans, delay);
        if (trans.get_response_status() == tlm::TLM_OK_RESPONSE) {
            std::memcpy(&reg_rdata, buf, 4);
        }
    }

    // Error checking to ensure good SRAM transaction
    if (trans.get_response_status() != tlm::TLM_OK_RESPONSE) {
        reg_status |= STAT_ERR;
    }

    // Completed transaction: set BUSY=0, DONE=1.
    reg_status &= ~STAT_BUSY;
    reg_status |= STAT_DONE;
    update_irq();
}

/*
    CPU accesses this mem_controller as if it has memory-mapped registers.
    b_transport decodes the address and either:
        Updates an internal register (WRITE).
        Returns an internal register’s value (READ).
*/
void Mem_Controller::b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay) {
    auto cmd  = trans.get_command();       // READ or WRITE
    auto addr = static_cast<uint32_t>(trans.get_address()); // Register offset
    auto ptr  = trans.get_data_ptr();      // Data payload buffer
    auto len  = trans.get_data_length();   // Number of bytes (should be 4)

    // Enforce register alignment rules
    // Treat address as offset (since TB binds directly to MC)
    if (addr + len > REG_SPACE || len != 4 || (addr & 0x3)) {
        trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
        return;
    }

    uint32_t data = 0;
    // WRITE, write to specified registers based on what the CPU requests
    if (cmd == tlm::TLM_WRITE_COMMAND) {
        std::memcpy(&data, ptr, 4); // Copy received data into data variable for assignment
        
        switch (addr) {
            case REG_CONTROL:
                reg_control = data;
                
                // Start operation if reg_control contains CTRL_START
                if (reg_control & CTRL_START) {
                    start_operation(delay);
                }
                break;

            case REG_ADDR:
                reg_addr = data;
                break;
            case REG_LEN:
                reg_len = data;
                break;
            case REG_WDATA:
                reg_wdata = data;
                break;

            // W1C (write-one-to-clear) for DONE and ERR bits (if CPU writes 1 to DONE, it clears DONE, If CPU writes 1 to ERR, it clears ERR.)
            case REG_STATUS:
                if (data & STAT_DONE) {
                    reg_status &= ~STAT_DONE;
                }
                if (data & STAT_ERR) {
                    reg_status &= ~STAT_ERR;
                }
                update_irq();
                break;

            default:
                trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
                return;
        }
    
    // READ, send data from respective registers to the CPU
    } else if (cmd == tlm::TLM_READ_COMMAND) {
        switch (addr) {
            case REG_CONTROL: data = reg_control; break;
            case REG_ADDR:    data = reg_addr;    break;
            case REG_LEN:     data = reg_len;     break;
            case REG_WDATA:   data = reg_wdata;   break;
            case REG_RDATA:   data = reg_rdata;   break;
            case REG_STATUS:  data = reg_status;  break;
            default:
                trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
                return;
        }
        std::memcpy(ptr, &data, 4);
    
    // ERROR
    } else {
        trans.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
        return;
    }

    trans.set_response_status(tlm::TLM_OK_RESPONSE);
}