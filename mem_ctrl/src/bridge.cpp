// bridge.cpp

#include "include/bridge.h"

using namespace sc_core;
using namespace tlm;

void Bridge::run() {
    delay = sc_core::SC_ZERO_TIME;

    // Init UNIX socket, path at /tmp/systemc.sock
    int server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, "/tmp/systemc.sock");
    unlink(addr.sun_path);

    // Bind address to server and listen
    bind(server_fd, (sockaddr*)&addr, sizeof(addr));
    listen(server_fd, 1);

    printf("[SystemC] Waiting for QEMU...\n");
    int client_fd = accept(server_fd, nullptr, nullptr);
    printf("[SystemC] Connected to QEMU\n");

    /* Server loop */
    while (true) {
        bridge_msg msg{};
        ssize_t n = recv(client_fd, &msg, sizeof(msg), 0);
        
        // If no data received, connection closed
        if (n <= 0) {
            printf("[SystemC] Client disconnected\n");
            break;  // or wait for new connection
        }

        // TODO: may have to use ctrl_wait() after setting the REG_CONTROL register to START to ensure controller operations are complete before moving on (e.g trying to read data being requested)
        printf("[DEBUG] Received: is_write: %d, is_dma: %d, addr: 0x%lx, size: %d \n", msg.is_write, msg.is_dma, msg.addr, msg.size);

        /* Control register write */
        if (msg.is_write && !msg.is_dma) {
            
            // Take first 4 bytes of msg.data for the control data
            uint32_t ctrl = 0;
            memcpy(&ctrl, msg.data, sizeof(ctrl));
            msg.status = mmio_write(msg.addr, ctrl);
            
            if (msg.status != tlm::TLM_OK_RESPONSE) {
                printf("[ERROR] WRITE FAILED: addr=0x%lx, status=%d\n", msg.addr, msg.status);
            }       
            printf("[DEBUG] WRITE:    is_write: %d, addr: 0x%lx, data: 0x%x, size: %d \n\n", msg.is_write, msg.addr, ctrl, msg.size);
        
        /* DMA block write */
        } else if (msg.is_write && msg.is_dma) {
            msg.status = mmio_write_block(msg.addr, msg.data, msg.size);

            if (msg.status != tlm::TLM_OK_RESPONSE) {
                printf("[ERROR] DMA WRITE FAILED: addr=0x%lx size=%d\n", msg.addr, msg.size);
            }
            printf("[DEBUG] DMA WRITE: addr=0x%lx size=%d\n\n", msg.addr, msg.size);

        /* Control register read */
        } else {
            uint32_t read_data;
            msg.status = mmio_read(msg.addr, read_data);
            
            // Write read_data (control register value) into the first 4 bytes of msg.data
            memcpy(msg.data, &read_data, sizeof(read_data));
            
            if (msg.status != tlm::TLM_OK_RESPONSE) {
                printf("[ERROR] READ FAILED: addr=0x%lx, status=%d\n", msg.addr, msg.status);
            }
            printf("[DEBUG] READ:     is_write: %d, addr: 0x%lx, data: 0x%x, size: %d \n\n", msg.is_write, msg.addr, read_data, msg.size);
        }

        send(client_fd, &msg, sizeof(msg), 0);
    }
}

// Helper function to write to mmio
tlm::tlm_response_status Bridge::mmio_write(uint32_t addr_offset, uint32_t value) {    
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
    tlm_socket->b_transport(trans, delay);

    return trans.get_response_status();
};

// Helper function to write bulk data to mmio
tlm::tlm_response_status Bridge::mmio_write_block(uint32_t addr_offset, uint8_t *value, uint32_t len) {    
    tlm::tlm_generic_payload trans;
    trans.set_command(tlm::TLM_WRITE_COMMAND);
    trans.set_address(addr_offset);
    trans.set_data_length(len);
    trans.set_streaming_width(len);
    trans.set_byte_enable_ptr(nullptr);
    trans.set_dmi_allowed(false);
    trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
    trans.set_data_ptr(value);

    tlm_socket->b_transport(trans, delay);

    return trans.get_response_status();
};

// Helper function to read from mmio
tlm::tlm_response_status Bridge::mmio_read(uint32_t addr_offset, uint32_t& value) {
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
    tlm_socket->b_transport(trans, delay);
    value = v;
    return trans.get_response_status();
};

void Bridge::ctrl_wait() {
    // Skip waiting if already completed
    if (!irq.read()) wait(irq.posedge_event());
    
    // Clear status
    mmio_write(REG_STATUS, STAT_DONE);
    wait(SC_ZERO_TIME);
}

// void Bridge::dma_wait() {    
//     // // Check if DMA already completed
//     // if (dma_irq.read()) {
//     //     printf("  CPU: DMA already completed (signal HIGH)\n");
//     // } else {
//     //     printf("  CPU: Waiting for DMA interrupt...\n");
//     //     wait(dma_irq.posedge_event());
//     //     printf("  CPU: DMA IRQ received!\n");
//     // }

//     // Skip waiting if already completed
//     if (!dma_irq.read()) wait(dma_irq.posedge_event());
    
//     // Clear DMA status
//     mmio_write(DMA_STATUS, DMA_DONE);
//     wait(SC_ZERO_TIME);
// }

// void Bridge::dma_transfer(const void* src_data, uint32_t size, uint32_t dst_addr) {
//     printf("CPU: Programming DMA for %d byte transfer to 0x%08X\n", size, dst_addr);

// #if HOST_64BIT
//     // Split 64-bit pointer into two 32-bit parts
//     uint64_t src_addr = reinterpret_cast<uint64_t>(src_data);
//     uint32_t addr_lo = static_cast<uint32_t>(src_addr & 0xFFFFFFFF);
//     uint32_t addr_hi = static_cast<uint32_t>((src_addr >> 32) & 0xFFFFFFFF);
    
//     printf("CPU: Source address: 0x%016lX (HI: 0x%08X, LO: 0x%08X)\n", src_addr, addr_hi, addr_lo);
    
//     mmio_write(DMA_SRC_ADDR_LO, addr_lo);
//     mmio_write(DMA_SRC_ADDR_HI, addr_hi);
// #else
//     // Direct 32-bit address
//     uint32_t src_addr = reinterpret_cast<uint32_t>(src_data);
//     printf("CPU: Source address: 0x%08X\n", src_addr);
    
//     mmio_write(DMA_SRC_ADDR, src_addr);
// #endif
//     mmio_write(DMA_DST_ADDR, dst_addr);
//     mmio_write(DMA_LENGTH, size);
//     mmio_write(DMA_CONTROL, DMA_START | DMA_IRQEN);
    
//     printf("CPU: DMA transfer programmed...\n");
// }

// // mmio_write a flattened float matrix one word at a time
// // Word size = 4 bytes, same as one float value
// void Bridge::load_matrix(float* matrix, int row_size, int col_size, uint32_t base_addr){
//     for(int row = 0; row < row_size; ++row){
//         for(int col = 0; col < col_size; ++col){
//             // Address works like [row][col], with sizeof(float) word alignment
//             uint32_t addr = base_addr + (((row * col_size) + col) * sizeof(float));
//             mmio_write(REG_ADDR, addr);
//             mmio_write(REG_LEN, 4);

//             // Bit-copy float into uint for transportation
//             uint32_t float_to_uint;
//             memcpy(&float_to_uint, &matrix[(row * col_size) + col], sizeof(float));

//             mmio_write(REG_WDATA, float_to_uint);
//             mmio_write(REG_CONTROL, CTRL_IRQEN | CTRL_WRITE | CTRL_START);
            
//             ctrl_wait();
//         }
//     }
// }

// // mmio_write a float vector one word at a time
// // Word size = 4 bytes, same as one float value
// void Bridge::load_f_vector(float* vector, int size, uint32_t base_addr){
//     for(int i = 0; i < size; ++i){
//         uint32_t addr = base_addr + (i * sizeof(float));
//         mmio_write(REG_ADDR, addr);
//         mmio_write(REG_LEN, 4);

//         // Bit-copy float into uint for transportation
//         uint32_t float_to_uint;
//         memcpy(&float_to_uint, &vector[i], 4);

//         mmio_write(REG_WDATA, float_to_uint);
//         mmio_write(REG_CONTROL, CTRL_IRQEN | CTRL_WRITE | CTRL_START);
        
//         ctrl_wait();
//     }
// }

// // mmio_write a uint8_t vector one word at a time, meaning 4 uint8_t packed into one word
// void Bridge::load_u8_vector(uint8_t* vector, int size, uint32_t base_addr){
//     int words_needed = (size + 3) / 4; // Round up to handle partial words
    
//     for(int word = 0; word < words_needed; ++word){
//         uint32_t addr = base_addr + (word * sizeof(uint32_t));
//         mmio_write(REG_ADDR, addr);
//         mmio_write(REG_LEN, 4);

//         // Pack 4 uint8_t values into one uint32_t word package by bit shifting each value into place
//         // Format is little-endian
//         uint32_t packed_word = 0;
//         for(int byte = 0; byte < 4; ++byte){
//             int index = (word * 4) + byte;
//             if(index < size){
//                 packed_word |= ((uint32_t)vector[index]) << (byte * 8);
//             }
//             // If index >= size, leave as 0 (padding)
//         }

//         mmio_write(REG_WDATA, packed_word);
//         mmio_write(REG_CONTROL, CTRL_IRQEN | CTRL_WRITE | CTRL_START);
        
//         ctrl_wait();
//     }
// }