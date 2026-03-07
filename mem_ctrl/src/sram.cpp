// sram.cpp

#include "include/sram.h"

using namespace sc_core;
using namespace tlm;

Sram::Sram(sc_module_name name):
      sc_module(name),
      socket("socket"),
      latency(10, SC_NS) {
    
    mem = new uint8_t[SRAM_SIZE](); // Create space for memory 
    socket.register_b_transport(this, &Sram::b_transport); // Register the b_transport function within the socket
}

Sram::~Sram() {
    delete[] mem;
}

void Sram::b_transport(tlm_generic_payload &trans, sc_time &delay) {
    // Load all data from payload
    tlm::tlm_command cmd = trans.get_command();
    uint32_t   addr = (uint32_t)trans.get_address();
    uint8_t*   ptr = trans.get_data_ptr();
    uint32_t   len = trans.get_data_length();
    uint8_t*   mask = trans.get_byte_enable_ptr();
    //uint32_t   wid = trans.get_streaming_width();

    // Start compute in memory operation
    // SRAM_COMPUTE_CMD is a special memory address that will tell SRAM to start computing and not read or write
    if(addr == SRAM_COMPUTE_CMD) {
        // Execute compute in memory
        compute_in_memory();
        trans.set_response_status(TLM_OK_RESPONSE);
        return;
    }
        
    // Error if memory access is outside valid memory space 
    if(addr + len > SRAM_SIZE) {
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

    // Write data from data pointer into mem
    } else if(cmd == TLM_WRITE_COMMAND) {
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

// TODO: add SRAM compute in memory delays here

/**
 * Perform a simulated fully-parallel forward pass of a single-layer neural network stored in SRAM
 *
 * Notes:
 *   - Pixel values are pre-normalized (before being written to device)to [0,1] by dividing by 255.
 */
void Sram::compute_in_memory() {
    float *weights  = (float*)&mem[WEIGHT_BASE_ADDR];
    float *bias     = (float*)&mem[BIAS_BASE_ADDR];
    float *in       = (float*)&mem[INPUT_BASE_ADDR];   /* Note: Pre-normalized image pixels */
    float *out      = (float*)&mem[OUTPUT_BASE_ADDR];

    // Functional computation
    for (int i = 0; i < MNIST_LABELS; i++) {
        float sum = bias[i];
        for (int j = 0; j < MNIST_IMAGE_SIZE; j++) {
            sum += in[j] * weights[i * MNIST_IMAGE_SIZE + j];
        }
        out[i] = sum;
    }

    // Timing model for a fully parallel dot-product engine and adder tree

    wait(sc_time(0, SC_NS));
}