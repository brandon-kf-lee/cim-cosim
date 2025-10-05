// testbench.cpp

#include "include/testbench.h"

using namespace sc_core;
using namespace tlm;

void Testbench::run() {
    delay = sc_core::SC_ZERO_TIME;

    // Load the pre-trained network
    neural_network_t network;
    FILE* mnist_network = fopen("binaries/mnist_network.bin", "rb");
    if (!mnist_network) {
        printf("Failed to open network file.\n");
        return;
    }
    fread(&network, sizeof(network), 1, mnist_network);
    fclose(mnist_network);

    // Load pre-processed MNIST number
    mnist_image_t number;
    FILE* mnist_number = fopen("binaries/mnist_0.bin", "rb");
    if(!mnist_number){
        printf("Failed to open number file.\n");
        return;
    }
    fread(&number, sizeof(number), 1, mnist_number);
    fclose(mnist_number);

    // Load weight matrix (10 x 784 matrix) using DMA
    //load_matrix((float*)network.W, MNIST_LABELS, MNIST_IMAGE_SIZE, WEIGHT_BASE_ADDR);
    dma_transfer(&network.W, sizeof(network.W), WEIGHT_BASE_ADDR);
    //dma_wait();

    // Load bias vector (10 x 1 vector)
    load_f_vector(network.b, MNIST_LABELS, BIAS_BASE_ADDR);
    //dma_transfer(&network.b, sizeof(network.b), BIAS_BASE_ADDR);
    //dma_wait();

    // Load MNIST number (28 x 28 matrix) (flattened)
    load_u8_vector(number.pixels, MNIST_IMAGE_SIZE, INPUT_BASE_ADDR);
    //dma_transfer(&number.pixels, sizeof(number.pixels), INPUT_BASE_ADDR);
    dma_wait();

    printf("\n\nCPU: All DMA transfers complete, starting compute operation...\n");
    
    // Start compute in memory
    mmio_write(REG_LEN, 4);
    mmio_write(REG_CONTROL, CTRL_COMPUTE | CTRL_IRQEN | CTRL_START);
    ctrl_wait();
    printf("CPU: Compute operation completed, reading results...\n");

    // Readback activations
    float activations[MNIST_LABELS];
    printf("CPU: Reading %d activation values...\n", MNIST_LABELS);
    for(int i = 0; i < MNIST_LABELS; ++i){
        uint32_t activation_addr = OUTPUT_BASE_ADDR + (i * sizeof(float));
        mmio_write(REG_ADDR, activation_addr);
        mmio_write(REG_LEN, 4);
        mmio_write(REG_CONTROL, CTRL_IRQEN | CTRL_START);
        ctrl_wait();

        uint32_t raw_activation = 0;
        mmio_read(REG_RDATA, raw_activation);
        memcpy(&activations[i], &raw_activation, sizeof(float));
    }

    // Run activations through softmax
    neural_network_softmax((float*)activations, MNIST_LABELS);

    // Set prediction to the index of the greatest activation
    int prediction = 0;
    float max_activation = activations[0];
    for (int i = 0; i < MNIST_LABELS; ++i) {
        printf("%d: %f\n", i, activations[i]);
        if (max_activation < activations[i]) {
            max_activation = activations[i];
            prediction = i;
        }
    }
    printf("Predicted number: %d\n", prediction);
}

// Helper function to write to mmio
void Testbench::mmio_write(uint32_t addr_offset, uint32_t value) {
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
    return;
};

// Helper function to read from mmio
void Testbench::mmio_read(uint32_t addr_offset, uint32_t& value) {
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
    return;
};
void Testbench::ctrl_wait() {
    // wait(irq.posedge_event());
    // mmio_write(REG_STATUS, STAT_DONE); // Clear status
    // wait(SC_ZERO_TIME);

    // if (irq.read()) {
    //     printf("  CPU: CTRL already completed (signal HIGH)\n");
    // } else {
    //     printf("  CPU: Waiting for CTRL interrupt...\n");
    //     wait(irq.posedge_event());
    //     printf("  CPU: CTRL IRQ received!\n");
    // }
    
    // Skip waiting if already completed
    if (!irq.read()) wait(irq.posedge_event());
    
    // Clear status
    mmio_write(REG_STATUS, STAT_DONE);
    wait(SC_ZERO_TIME);
}

void Testbench::dma_wait() {    
    // // Check if DMA already completed
    // if (dma_irq.read()) {
    //     printf("  CPU: DMA already completed (signal HIGH)\n");
    // } else {
    //     printf("  CPU: Waiting for DMA interrupt...\n");
    //     wait(dma_irq.posedge_event());
    //     printf("  CPU: DMA IRQ received!\n");
    // }

    // Skip waiting if already completed
    if (!dma_irq.read()) wait(dma_irq.posedge_event());
    
    // Clear DMA status
    mmio_write(DMA_STATUS, DMA_DONE);
    wait(SC_ZERO_TIME);
}

void Testbench::dma_transfer(const void* src_data, uint32_t size, uint32_t dst_addr) {
    printf("CPU: Programming DMA for %d byte transfer to 0x%08X\n", size, dst_addr);

#if HOST_64BIT
    // Split 64-bit pointer into two 32-bit parts
    uint64_t src_addr = reinterpret_cast<uint64_t>(src_data);
    uint32_t addr_lo = static_cast<uint32_t>(src_addr & 0xFFFFFFFF);
    uint32_t addr_hi = static_cast<uint32_t>((src_addr >> 32) & 0xFFFFFFFF);
    
    printf("CPU: Source address: 0x%016lX (HI: 0x%08X, LO: 0x%08X)\n", src_addr, addr_hi, addr_lo);
    
    mmio_write(DMA_SRC_ADDR_LO, addr_lo);
    mmio_write(DMA_SRC_ADDR_HI, addr_hi);
#else
    // Direct 32-bit address
    uint32_t src_addr = reinterpret_cast<uint32_t>(src_data);
    printf("CPU: Source address: 0x%08X\n", src_addr);
    
    mmio_write(DMA_SRC_ADDR, src_addr);
#endif
    mmio_write(DMA_DST_ADDR, dst_addr);
    mmio_write(DMA_LENGTH, size);
    mmio_write(DMA_CONTROL, DMA_START | DMA_IRQEN);
    
    printf("CPU: DMA transfer programmed...\n");
}

// mmio_write a flattened float matrix one word at a time
// Word size = 4 bytes, same as one float value
void Testbench::load_matrix(float* matrix, int row_size, int col_size, uint32_t base_addr){
    for(int row = 0; row < row_size; ++row){
        for(int col = 0; col < col_size; ++col){
            // Address works like [row][col], with sizeof(float) word alignment
            uint32_t addr = base_addr + (((row * col_size) + col) * sizeof(float));
            mmio_write(REG_ADDR, addr);
            mmio_write(REG_LEN, 4);

            // Bit-copy float into uint for transportation
            uint32_t float_to_uint;
            memcpy(&float_to_uint, &matrix[(row * col_size) + col], sizeof(float));

            mmio_write(REG_WDATA, float_to_uint);
            mmio_write(REG_CONTROL, CTRL_IRQEN | CTRL_WRITE | CTRL_START);
            
            ctrl_wait();
        }
    }
}

// mmio_write a float vector one word at a time
// Word size = 4 bytes, same as one float value
void Testbench::load_f_vector(float* vector, int size, uint32_t base_addr){
    for(int i = 0; i < size; ++i){
        uint32_t addr = base_addr + (i * sizeof(float));
        mmio_write(REG_ADDR, addr);
        mmio_write(REG_LEN, 4);

        // Bit-copy float into uint for transportation
        uint32_t float_to_uint;
        memcpy(&float_to_uint, &vector[i], 4);

        mmio_write(REG_WDATA, float_to_uint);
        mmio_write(REG_CONTROL, CTRL_IRQEN | CTRL_WRITE | CTRL_START);
        
        ctrl_wait();
    }
}

// mmio_write a uint8_t vector one word at a time, meaning 4 uint8_t packed into one word
void Testbench::load_u8_vector(uint8_t* vector, int size, uint32_t base_addr){
    int words_needed = (size + 3) / 4; // Round up to handle partial words
    
    for(int word = 0; word < words_needed; ++word){
        uint32_t addr = base_addr + (word * sizeof(uint32_t));
        mmio_write(REG_ADDR, addr);
        mmio_write(REG_LEN, 4);

        // Pack 4 uint8_t values into one uint32_t word package by bit shifting each value into place
        // Format is little-endian
        uint32_t packed_word = 0;
        for(int byte = 0; byte < 4; ++byte){
            int index = (word * 4) + byte;
            if(index < size){
                packed_word |= ((uint32_t)vector[index]) << (byte * 8);
            }
            // If index >= size, leave as 0 (padding)
        }

        mmio_write(REG_WDATA, packed_word);
        mmio_write(REG_CONTROL, CTRL_IRQEN | CTRL_WRITE | CTRL_START);
        
        ctrl_wait();
    }
}

/** 
 * TODO: Temp testing function to calculate final result vector
 * Calculate the softmax vector from the activations.
 */
void Testbench::neural_network_softmax(float* activations, int length) {
    int i;
    float sum, max;

    for (i = 1, max = activations[0]; i < length; i++) {
        if (activations[i] > max) {
            max = activations[i];
        }
    }

    for (i = 0, sum = 0; i < length; i++) {
        activations[i] = exp(activations[i] - max);
        sum += activations[i];
    }

    for (i = 0; i < length; i++) {
        activations[i] /= sum;
    }
}