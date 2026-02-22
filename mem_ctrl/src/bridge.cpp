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

        /* Mark time before controller does work */
        t_start = sc_core::sc_time_stamp();

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

        // TEST DELAY
        wait(1000, SC_NS);

        /* Mark time after controller finishes work */
        t_end = sc_core::sc_time_stamp();
        t_delta = t_end - t_start;
        msg.simulated_ns = static_cast<uint64_t>(t_delta.to_seconds() * 1e9);

        // TODO: Capture IRQ state and forward to sc_dev?

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