// testbench.cpp

#include "include/testbench.h"
#include "include/mem_controller_registers.h"

using namespace sc_core;
using namespace tlm;

void Testbench::run() {
    sc_core::sc_time delay = sc_core::SC_ZERO_TIME;

    // Helper lambda to write to mmio
    auto mmio_write = [&](uint32_t addr_offset, uint32_t value) {
        tlm::tlm_generic_payload trans;
        trans.set_command(tlm::TLM_WRITE_COMMAND);
        trans.set_address(addr_offset);
        trans.set_data_length(4);
        trans.set_streaming_width(4);
        trans.set_byte_enable_ptr(nullptr);
        trans.set_dmi_allowed(false);
        trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
        
        uint32_t v = value;
        trans.set_data_ptr(reinterpret_cast<unsigned char*>(&v));
        socket->b_transport(trans, delay);
    };

    // Helper lambda to read from mmio
    auto mmio_read = [&](uint32_t addr_offset, uint32_t& value) {
        tlm::tlm_generic_payload trans;
        trans.set_command(tlm::TLM_READ_COMMAND);
        trans.set_address(addr_offset);
        trans.set_data_length(4);
        trans.set_streaming_width(4);
        trans.set_byte_enable_ptr(nullptr);
        trans.set_dmi_allowed(false);
        trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
        
        uint32_t v = 0;
        trans.set_data_ptr(reinterpret_cast<unsigned char*>(&v));
        socket->b_transport(trans, delay);
        value = v;
    };


    // Pt 1: WRITE 0xDEADBEEF to SRAM address 0x10
    mmio_write(REG_ADDR, 0x00000010);
    mmio_write(REG_LEN,  4);
    mmio_write(REG_WDATA, 0xDEADBEEF);
    mmio_write(REG_CONTROL, CTRL_IRQEN | CTRL_WRITE | CTRL_START);

    // Wait for interrupt (or poll STATUS until DONE)
    wait(irq.posedge_event());
    
    // Check what the data to write was
    uint32_t wdata = 0;
    mmio_read(REG_WDATA, wdata);
    std::printf("Write WDATA = 0x%08X\n", wdata);

    // Ack DONE (W1C)
    mmio_write(REG_STATUS, STAT_DONE);

    // Pt 2: READ back from SRAM address 0x10 (No CTRL_WRITE is assumed as a read op)
    mmio_write(REG_ADDR, 0x00000010);
    mmio_write(REG_LEN,  4);
    mmio_write(REG_CONTROL, CTRL_IRQEN | CTRL_START);

    wait(irq.posedge_event());

    // Read data
    uint32_t rdata = 0;
    mmio_read(REG_RDATA, rdata);

    // Ack DONE
    mmio_write(REG_STATUS, STAT_DONE);
    std::printf("Read RDATA = 0x%08X\n", rdata);
}



// void Testbench::run() {
//     // TODO: change testbench (CPU) to send a "RISC-V-like" command (with command type, memory adresses, data) to the memory controller
//     //       memory controller (driver) parses command, then sends direct command to sram,
//     //       sram returns result to mem_controller, mem_controller notifies (interrupt?) cpu of successful operation
//     // (or do it memory mapped? like cpu/testbench accesses a part of mem_controller memory and modifies sram that way)"
   
//     sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
    
//     uint8_t data[4];
//     uint32_t value = 0xDEADBEEF;
//     memcpy(data, &value, 4);

//     // Initialize payload
//     tlm_generic_payload trans;
//     trans.set_command(TLM_WRITE_COMMAND);
//     trans.set_address(0x10);
//     trans.set_data_ptr(data);
//     trans.set_data_length(4);

//     // Send payload through socket
//     socket->b_transport(trans, delay);
//     printf("Wrote 0x%X to SRAM, status: %s\n", value, trans.get_response_string().c_str());

//     // Reset data buffer and send read command
//     memset(data, 0, 4);
//     trans.set_command(TLM_READ_COMMAND);
//     socket->b_transport(trans, delay);

//     // Display result
//     memcpy(&value, data, 4);
//     printf("Read 0x%X from SRAM, status: %s\n", value, trans.get_response_string().c_str());
// }
