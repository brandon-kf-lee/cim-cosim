// sram.cpp

#include "include/sram.h"

Sram::Sram(sc_module_name name, uint32_t size_bytes):
      sc_module(name),
      socket("socket"),
      mem_size(size_bytes),
      latency(10, SC_NS) {
    
    mem = new uint8_t[mem_size](); // Create space for memory 
    socket.register_b_transport(this, &Sram::b_transport); // Register the b_transport function within the socket
}

void Sram::b_transport(tlm_generic_payload &trans, sc_time &delay) {
    // Load all data from payload
    tlm::tlm_command cmd = trans.get_command();
    uint32_t   addr = (uint32_t)trans.get_address();
    uint8_t*   ptr = trans.get_data_ptr();
    uint32_t   len = trans.get_data_length();
    uint8_t*   mask = trans.get_byte_enable_ptr();
    //uint32_t   wid = trans.get_streaming_width();

    // Error if memory access is outside valid memory space 
    if(addr + len > mem_size) {
        trans.set_response_status(TLM_ADDRESS_ERROR_RESPONSE);
        return;
    } 

    // Read data from mem into data pointer
    if(cmd == TLM_READ_COMMAND) {
        // No bit mask, straight copy
        if(!mask) {
            memcpy(ptr, &mem[addr], len);
        }
        // Respect bit mask
        else {
            for(unsigned i = 0; i < len; ++i) {
                if(mask[i % trans.get_byte_enable_length()]) {
                    ptr[i] = mem[addr + i];
                }
            }
        }
    }

    // Write data from data pointer into mem
    else if(cmd == TLM_WRITE_COMMAND) {
        if(!mask) {
            memcpy(&mem[addr], ptr, len);
        }
        else {
            for(unsigned i = 0; i < len; ++i){
                if(mask[i % trans.get_byte_enable_length()]) {
                    mem[addr + i] = ptr[i];
                }
            }
        }
    }

    // Set delay for these memory accesses
    delay += latency;
    trans.set_response_status(TLM_OK_RESPONSE);

}