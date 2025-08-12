// testbench.cpp

#include "include/testbench.h"

void Testbench::run() {
    sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
    
    uint8_t data[4];
    uint32_t value = 0xDEADBEEF;
    memcpy(data, &value, 4);

    // Initialize payload
    tlm_generic_payload trans;
    trans.set_command(TLM_WRITE_COMMAND);
    trans.set_address(0x10);
    trans.set_data_ptr(data);
    trans.set_data_length(4);

    // Send payload through socket
    socket->b_transport(trans, delay);
    printf("Wrote 0x%X to SRAM, status: %s\n", value, trans.get_response_string().c_str());

    // Reset data buffer and send read command
    memset(data, 0, 4);
    trans.set_command(TLM_READ_COMMAND);
    socket->b_transport(trans, delay);

    // Display result
    memcpy(&value, data, 4);
    printf("Read 0x%X from SRAM, status: %s\n", value, trans.get_response_string().c_str());
}