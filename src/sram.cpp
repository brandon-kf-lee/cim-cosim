// sram.cpp

#include "include/sram.h"

using namespace sc_core;
using namespace tlm;

Sram::Sram(sc_module_name name, uint32_t size_bytes):
      sc_module(name),
      socket("socket"),
      mem_size(size_bytes),
      latency(10, SC_NS) {
    
    mem = new uint8_t[mem_size](); // Create space for memory 
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
        //Spawn parallel computation
        sc_spawn(sc_bind(&Sram::compute_in_memory, this));
    }
        
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
 * This function reads input pixels, weights, and biases from memory, computes
 * the dot product for each neuron in parallel, and writes the resulting
 * activations back to memory. Each neuron computes its output independently,
 * and each multiplication within a neuron is also computed in parallel.
 *
 * Synchronization:
 *   - Uses sc_event and counters to ensure all multiplications and neurons
 *     complete before returning.
 *
 * Notes:
 *   - Pixel values are normalized to [0,1] by dividing by 255.
 *   - Each neuron computation and every multiplication within it spawns a separate SystemC thread.
 */
void Sram::compute_in_memory() {
    float* weights = (float*)&mem[WEIGHT_BASE_ADDR];
    float* bias = (float*)&mem[BIAS_BASE_ADDR];
    uint8_t* pixels = (uint8_t*)&mem[INPUT_BASE_ADDR];
    float* activations = (float*)&mem[OUTPUT_BASE_ADDR];

    int neuron_remaining = MNIST_LABELS; // Counter + event to track how many neurons left for synchronization
    sc_event all_done;

    // Parallelized rows (propage the image through each neuron) 
    // Copy local variables by value (except synchronization variables) into lambda
    for(int i = 0; i < MNIST_LABELS; i++) {
    printf("Neuron %d spawned.\n", i);
    sc_spawn([=, &all_done, &neuron_remaining]() {
            float sum = bias[i]; // Initialize final dot product with bias
            float partial_mult[MNIST_IMAGE_SIZE]; // Store each intermediate pixel x weight calculation
            int mult_remaining = MNIST_IMAGE_SIZE; // Counter + event to track how many multiplications left for synchronization
            sc_event mult_done;

            // Parallelized columns (728 pixels x 728 weights, all done simultaneously)
            // Copy variables by reference (except j) into lambda
            for(int j = 0; j < MNIST_IMAGE_SIZE; j++) {
                sc_spawn([&, j]() {
                    float normalized = (float)pixels[j] / 255.0f;
                    partial_mult[j] = normalized * weights[i * MNIST_IMAGE_SIZE + j];

                    // Decrement multiplier counter (no race conditions, SystemC guarantees exclusive non-prememptive control over data)
                    mult_remaining--;
                    if (mult_remaining == 0) {
                        mult_done.notify(); // Notify when all multipliers finish
                    }
                });
            }
            wait(mult_done);

            // Adder tree + modeled delay
            for(int j = 0; j < MNIST_IMAGE_SIZE; j++) {
                sum += partial_mult[j];
            }
            wait(SC_ZERO_TIME); 
            
            activations[i] = sum;

            // Decrement neuron counter atomically
            neuron_remaining--;
            if (neuron_remaining == 0) {
                all_done.notify(); // Notify when all multipliers finish
            }
            printf("Neuron %d finished.\n", i);
        });
    }
    wait(all_done); // All rows complete in parallel
}
