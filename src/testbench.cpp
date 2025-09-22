// testbench.cpp

#include "include/testbench.h"
#include "include/mem_controller_registers.h"
#include "include/mnist_network.h"

using namespace sc_core;
using namespace tlm;

void Testbench::run() {
    delay = sc_core::SC_ZERO_TIME;

    // Load the pre-trained network
    neural_network_t network;
    FILE* mnist_network = fopen("include/mnist_network.bin", "rb");
    if (!mnist_network) {
        printf("Failed to open network file.\n");
        return;
    }

    fread(&network, sizeof(network), 1, mnist_network);
    fclose(mnist_network);

    // Load weight matrix (10 x 784 matrix)
    load_matrix((float*)network.W, MNIST_LABELS, MNIST_IMAGE_SIZE, WEIGHT_BASE_ADDR);

    // Load bias vector (10 x 1 vector)
    load_vector((float*)network.b, MNIST_LABELS, BIAS_BASE_ADDR);



    // Test 1: Random Weight Test
    printf("\n=== Testing Random Weight ===\n");
    int test_row = 5;
    int test_col = 129;
    printf("Expected: network.W[%d][%d] = %f\n", test_row, test_col, network.W[test_row][test_col]);
    
    uint32_t target_addr = WEIGHT_BASE_ADDR + (((test_row * MNIST_IMAGE_SIZE) + test_col) * sizeof(float));
    mmio_write(REG_ADDR, target_addr);
    mmio_write(REG_LEN, 4);
    mmio_write(REG_CONTROL, CTRL_IRQEN | CTRL_START);
    wait(irq.posedge_event());
    mmio_write(REG_STATUS, STAT_DONE);
    wait(SC_ZERO_TIME);
    
    uint32_t rdata = 0;
    mmio_read(REG_RDATA, rdata);
    float weight_temp;
    memcpy(&weight_temp, &rdata, 4);
    printf("MMIO read: Weight[%d][%d] = %f\n", test_row, test_col, weight_temp);
    
    if (network.W[test_row][test_col] == weight_temp) {
        printf("Weight test PASSED\n");
    } else {
        printf("Weight test FAILED\n");
    }

    // Test 2: Complete Bias Vector Test  
    printf("\n=== Testing Complete Bias Vector ===\n");
    int bias_pass_count = 0;
    
    for (int i = 0; i < MNIST_LABELS; i++) {
        uint32_t bias_addr = BIAS_BASE_ADDR + (i * sizeof(float));
        mmio_write(REG_ADDR, bias_addr);
        mmio_write(REG_LEN, 4);
        mmio_write(REG_CONTROL, CTRL_IRQEN | CTRL_START);
        wait(irq.posedge_event());
        mmio_write(REG_STATUS, STAT_DONE);
        wait(SC_ZERO_TIME);
        
        uint32_t bias_rdata = 0;
        mmio_read(REG_RDATA, bias_rdata);
        float bias_temp;
        memcpy(&bias_temp, &bias_rdata, 4);
        
        printf("bias[%d]: expected=%f, readback=%f", i, network.b[i], bias_temp);
        
        if (network.b[i] == bias_temp) {
            printf(" PASS\n");
            bias_pass_count++;
        } else {
            printf(" FAIL\n");
        }
    }
    
    printf("Bias results: %d/%d passed\n", bias_pass_count, MNIST_LABELS);
    if (bias_pass_count == MNIST_LABELS) {
        printf("All bias values correct\n");
    } else {
        printf("%d bias values failed\n", MNIST_LABELS - bias_pass_count);
    }

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
            
            wait(irq.posedge_event());
            mmio_write(REG_STATUS, STAT_DONE);
            wait(SC_ZERO_TIME);
        }
    }
}

// mmio_write a float vector one word at a time
// Word size = 4 bytes, same as one float value
void Testbench::load_vector(float* vector, int size, uint32_t base_addr){
    for(int i = 0; i < size; ++i){
        uint32_t addr = base_addr + (i * sizeof(float));
        mmio_write(REG_ADDR, addr);
        mmio_write(REG_LEN, 4);

        // Bit-copy float into uint for transportation
        uint32_t float_to_uint;
        memcpy(&float_to_uint, &vector[i], 4);
        mmio_write(REG_WDATA, float_to_uint);
        mmio_write(REG_CONTROL, CTRL_IRQEN | CTRL_WRITE | CTRL_START);
        
        wait(irq.posedge_event());
        mmio_write(REG_STATUS, STAT_DONE);
        wait(SC_ZERO_TIME);
    }
}