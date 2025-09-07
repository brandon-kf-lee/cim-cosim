// mem_controller.cpp

#include "include/mem_controller.h"


// Memory controller constructor, target for CPU module, initiating for SRAM module
Mem_Controller::Mem_Controller(sc_module_name name): 
    sc_module(name), 
    t_cpu_socket("t_cpu_socket"), 
    i_sram_socket("i_sram_socket") {
    
    t_cpu_socket.register_b_transport(this, &Mem_Controller::b_transport);
}

// Transaction translation and relay
void Mem_Controller::b_transport(tlm_generic_payload &trans, sc_core::sc_time &delay)
{
    // Example: print debug info
    std::cout << "[MC] Received transaction, forwarding to SRAM..." << std::endl;

    // Optional: translate transation to SRAM access (e.g., implement address remap, CIM decode, etc.)

    // Forward to SRAM
    i_sram_socket->b_transport(trans, delay);

    // Optional: post-process after SRAM response
}


// void Mem_Controller::run() {
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