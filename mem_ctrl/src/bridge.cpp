// bridge.cpp

#include "include/bridge.h"

using namespace sc_core;
using namespace tlm;

static bool recv_all(int fd, void *buf, size_t len)
{
    uint8_t *p = (uint8_t*)buf;
    while (len) {
        ssize_t n = recv(fd, p, len, MSG_WAITALL);
        if (n <= 0) return false;
        p += n;
        len -= (size_t)n;
    }
    return true;
}

static bool send_all(int fd, const void *buf, size_t len)
{
    const uint8_t *p = (const uint8_t*)buf;
    while (len) {
        ssize_t n = send(fd, p, len, 0);
        if (n <= 0) return false;
        p += n;
        len -= (size_t)n;
    }
    return true;
}

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

    /* 32-byte header, on stack. 16MB payload on heap */
    bridge_msg msg;
    uint8_t* payload = new uint8_t[DMA_BUFFER_SIZE];

    /* Server loop */
    while (true) {
        /* Bridge message header */
        if (!recv_all(client_fd, &msg, BRIDGE_HEADER_SIZE)) {
            fprintf(stderr, "[SystemC] Client disconnected\n");
            break;
        }

        /* Read payload only when present */
        if (msg.size > 0) {
            if (msg.size > DMA_BUFFER_SIZE) {
                fprintf(stderr, "[ERROR] size %u exceeds limits\n", msg.size);

                /* respond with error header only (no payload) */
                msg.status = tlm::TLM_BURST_ERROR_RESPONSE;
                msg.simulated_ns = 0;
                msg.ctrl_irq = irq.read();
                send_all(client_fd, &msg, BRIDGE_HEADER_SIZE);
                continue;
            }
        
            if (msg.is_write) {
                /* For writes, payload comes from QEMU */
                if (!recv_all(client_fd, payload, msg.size)) {
                    fprintf(stderr, "[SystemC] Client disconnected during payload\n");
                    break;
                }
            }
        }
        
        /* Mark time before controller does work */
        t_start = sc_core::sc_time_stamp();

        /* Control register write */
        if (msg.is_write && !msg.is_dma) {
            
            // Take first 4 bytes of payload for the control data
            uint32_t ctrl = 0;
            memcpy(&ctrl, payload, sizeof(ctrl));
            msg.status = mmio_write(msg.addr, ctrl);
            
            if (msg.status != tlm::TLM_OK_RESPONSE) {
                fprintf(stderr, "[ERROR] WRITE FAILED: addr=0x%lx, status=%d\n", msg.addr, msg.status);
            }       
            //printf("[DEBUG] WRITE:    is_write: %d, addr: 0x%lx, data: 0x%x, size: %d \n\n", msg.is_write, msg.addr, ctrl, msg.size);
        
        /* Control register read */
        } else if (!msg.is_write && ! msg.is_dma) {
            uint32_t read_data;
            msg.status = mmio_read(msg.addr, read_data);
            
            // Write read_data (control register value) into the first 4 bytes of payload
            memcpy(payload, &read_data, sizeof(read_data));
            
            if (msg.status != tlm::TLM_OK_RESPONSE) {
                fprintf(stderr, "[ERROR] READ FAILED: addr=0x%lx, status=%d\n", msg.addr, msg.status);
            }
            //printf("[DEBUG] READ:     is_write: %d, addr: 0x%lx, data: 0x%x, size: %d \n\n", msg.is_write, msg.addr, read_data, msg.size);

        /* DMA block write */
        } else if (msg.is_write && msg.is_dma) {
            msg.status = mmio_write_block(msg.addr, payload, msg.size);

            if (msg.status != tlm::TLM_OK_RESPONSE) {
                printf("[ERROR] DMA WRITE FAILED: addr=0x%lx size=%d\n", msg.addr, msg.size);
            }
            //printf("[DEBUG] DMA WRITE: addr=0x%lx size=%d\n\n", msg.addr, msg.size);
        
        /* DMA block read */
        } else if (!msg.is_write && msg.is_dma) {
            msg.status = mmio_read_block(msg.addr, payload, msg.size);

            if (msg.status != tlm::TLM_OK_RESPONSE) {
                printf("[ERROR] DMA READ FAILED: addr=0x%lx size=%d\n", msg.addr, msg.size);
            }
            //printf("[DEBUG] DMA READ: addr=0x%lx size=%d\n\n", msg.addr, msg.size);
        
        /* Unknown command */
        } else {
            fprintf(stderr, "[ERROR] Unknown command.\n");
        }

        /* Accumulate delays from all SystemC modules */
        if (delay != SC_ZERO_TIME) {
            wait(delay);
            delay = SC_ZERO_TIME;
        }

        // TEST DELAY
        //wait(1000, SC_NS);

        /* Mark time after controller finishes work */
        t_end = sc_core::sc_time_stamp();
        t_delta = t_end - t_start;
        msg.simulated_ns = static_cast<uint64_t>(t_delta.to_seconds() * 1e9);

        /* Read memory controller's current IRQ state and forward to QEMU */ 
        msg.ctrl_irq = irq.read();
        
        /* send response header */
        if (!send_all(client_fd, &msg, BRIDGE_HEADER_SIZE)) {
            fprintf(stderr, "[SystemC] send failed\n");
            break;
        }

        /* send payload only for reads with size>0 */
        if (!msg.is_write && msg.size > 0) {
            if (!send_all(client_fd, payload, msg.size)) {
                fprintf(stderr, "[SystemC] send payload failed\n");
                break;
            }
        }
    }
    
    delete[] payload;
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

// Helper function to read bulk data from mmio
tlm::tlm_response_status Bridge::mmio_read_block(uint32_t addr_offset, uint8_t *dst, uint32_t len)
{
    if (!dst || len == 0) {
        return tlm::TLM_GENERIC_ERROR_RESPONSE;
    }

    tlm::tlm_generic_payload trans;
    trans.set_command(tlm::TLM_READ_COMMAND);
    trans.set_address(addr_offset);
    trans.set_data_length(len);
    trans.set_streaming_width(len);
    trans.set_byte_enable_ptr(nullptr);
    trans.set_dmi_allowed(false);
    trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);

    trans.set_data_ptr(reinterpret_cast<unsigned char*>(dst));

    tlm_socket->b_transport(trans, delay);

    return trans.get_response_status();
}