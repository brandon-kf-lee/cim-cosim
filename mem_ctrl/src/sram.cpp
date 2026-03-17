// sram.cpp

#include "include/sram.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

using namespace sc_core;
using namespace tlm;

// Clamp to unsigned 4-bit range [0, 15].
// These values are stored in int8_t containers for simplicity, but enforce 4b semantics.
static inline uint8_t clamp_u4(int v)
{
    if (v < 0)  return 0;
    if (v > 15) return 15;
    return (uint8_t)v;
}

// Clamp to signed 4-bit range [-8, 7].
static inline int8_t clamp_s4(int v) {
    if (v < -8) return -8;
    if (v >  7) return  7;
    return (int8_t)v;
}

Sram::Sram(sc_module_name name):
    sc_module(name),
    socket("socket")
{
    mem = new uint8_t[SRAM_SIZE]();
    socket.register_b_transport(this, &Sram::b_transport);
}

Sram::~Sram()
{
    delete[] mem;
}

void Sram::b_transport(tlm_generic_payload &trans, sc_time &delay)
{
    // Load all data from payload
    tlm::tlm_command cmd = trans.get_command();
    uint32_t addr = (uint32_t)trans.get_address();
    uint8_t *ptr  = trans.get_data_ptr();
    uint32_t len  = trans.get_data_length();
    uint8_t *mask = trans.get_byte_enable_ptr();

    // Compute trigger (special address)
    if (addr == SRAM_COMPUTE_CMD) {
        // model compute time into delay
        compute_in_memory(delay);

        trans.set_response_status(TLM_OK_RESPONSE);
        return;
    }

    // Error if memory access is outside valid memory space 
    if (addr + len > SRAM_SIZE) {
        trans.set_response_status(TLM_ADDRESS_ERROR_RESPONSE);
        return;
    }

    // normal memory read/write
    if (cmd == TLM_READ_COMMAND) {
        // No bit mask, straight copy
		if (!mask) {
            std::memcpy(ptr, &mem[addr], len);
       
        // Respect bit mask
        } else {
            for (unsigned i = 0; i < len; ++i) {
                if (mask[i % trans.get_byte_enable_length()]) {
                    ptr[i] = mem[addr + i];
                }
            }
        }
    
    // Write data from data pointer into mem
    } else if (cmd == TLM_WRITE_COMMAND) {
        if (!mask) {
            std::memcpy(&mem[addr], ptr, len);
        } else {
            for (unsigned i = 0; i < len; ++i) {
                if (mask[i % trans.get_byte_enable_length()]) {
                    mem[addr + i] = ptr[i];
                }
            }
        }
    }

    // Add base SRAM latency
    delay += timing::SRAM_RW;
    trans.set_response_status(TLM_OK_RESPONSE);
}

/**
 * compute_in_memory(): 4b/4b FC (fully connected) inference with tile-based timing
 * Based on digital SRAM CIM macro developed by You et al. (2024)
 *  - weights: 4 bit (stored in int8) values (expected in [-8,7]) at WEIGHT_BASE_ADDR, layout [label][pixel]
 *  - bias:    4 byte (int32) at BIAS_BASE_ADDR, length MNIST_LABELS
 *  - input:   4 bit (stored in int8) values (expected in [-8,7]) at INPUT_BASE_ADDR, length MNIST_IMAGE_SIZE
 *  - output:  4 byte (int32) at OUTPUT_BASE_ADDR, length MNIST_LABELS
 *
 * Functional model:
 *   y[i] = b[i] + sum_j x[j] * W[i][j]
 *
 * Tiling model (macro size 64x64):
 *  - Single MNIST doesn't fit on the the 64x64 macro, so must be split into "tiles"
 *  - 64-wide tiles like a 64x64 macro => 13 tiles
 *
 * Timing model
 *   - multi-bit MAC requires ~M clock cycles per set of operations on average, where M = input bitwidth
 *   - for 4b inputs， ~4 cycles per tile
 *   - You et al. measured 4b/4b at 100 MHz, so 10 ns/cycle
 *   Example
 *      => tile time = 4 cycles * 10 ns = 40 ns/tile
 *      => total compute time per inference ≈ 13 tiles * 40 ns = 520 ns
 */
void Sram::compute_in_memory(sc_core::sc_time &delay)
{
    // Basic safety: ensure regions do not overlap SRAM end
    const uint32_t w_end = WEIGHT_BASE_ADDR + WEIGHT_SIZE;
    const uint32_t b_end = BIAS_BASE_ADDR   + BIAS_SIZE;
    const uint32_t x_end = INPUT_BASE_ADDR  + INPUT_SIZE;
    const uint32_t y_end = OUTPUT_BASE_ADDR + OUTPUT_SIZE;

    if (y_end > SRAM_SIZE) {
        SC_REPORT_ERROR("SRAM", "CIM regions exceed SRAM_SIZE");
        return;
    }

    // Derive number of tiles for 64-wide macro mapping
    const int tiles = (MNIST_IMAGE_SIZE + timing::TILE_W - 1) / timing::TILE_W; // 13 tiles for 784 pixels and 64x64 macro size

    // Typed pointers into/from the SRAM backing store
    int8_t  *W = reinterpret_cast<int8_t*>(&mem[WEIGHT_BASE_ADDR]);
    int32_t *b = reinterpret_cast<int32_t*>(&mem[BIAS_BASE_ADDR]);
    uint8_t  *x = reinterpret_cast<uint8_t*>(&mem[INPUT_BASE_ADDR]);
    int32_t *y = reinterpret_cast<int32_t*>(&mem[OUTPUT_BASE_ADDR]);

    // ---- Functional computation (tiled dot products) ----
    for (int i = 0; i < MNIST_LABELS; i++) {
        // Start with bias for this output neuron/class
        int32_t acc = b[i];

        // Accumulate in tiles of 64 inputs to reflect 64-wide CIM macro mapping
        for (int t = 0; t < tiles; t++) {
            const int base = t * timing::TILE_W;
            const int end  = std::min((base + (int)timing::TILE_W), (int)MNIST_IMAGE_SIZE);

            // Dot-product chunk: x[base:end] · W[i][base:end]
            for (int j = base; j < end; j++) {
                // Enforce signed-4b semantics even though stored as int8
                const uint8_t xv = clamp_u4((int)x[j]);
                const int8_t wv = clamp_s4((int)W[i * MNIST_IMAGE_SIZE + j]);

                // Multiply-accumulate into 32-bit accumulator
                acc += (int32_t)xv * (int32_t)wv;
            }
        }
        // Store final logit to output region
        y[i] = acc;
    }

    delay += timing::cim_time(tiles);
}