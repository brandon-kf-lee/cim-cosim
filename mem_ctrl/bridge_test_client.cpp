
/* TODO:
    This code emulates what the low level libraries used by application code in QEMU would
    do. 

    bridge_msg status codes are secretly tlm_response_status codes. ensure that error codes 
    and their meanings are properly relayed

*/


#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "include/mem_controller_registers.h"

#define TEST_ADDR   0x1000
#define DMA_BUFFER_SIZE 4096

struct bridge_msg {
    uint8_t  is_write;
    uint8_t  is_dma;
    uint16_t reserved;

    uint64_t addr;
    uint32_t size;

    uint8_t  data[DMA_BUFFER_SIZE];

    int8_t status;
} __attribute__((packed));

// Helper to create a message with a uint32_t data value
struct bridge_msg make_msg(uint8_t is_write, uint8_t is_dma, uint64_t addr, uint32_t data_val) {
    struct bridge_msg msg = {0};
    msg.is_write = is_write;
    msg.is_dma = is_dma;
    msg.addr = addr;
    msg.size = sizeof(data_val);
    memcpy(msg.data, &data_val, sizeof(data_val));
    return msg;
}

struct bridge_msg make_dma_msg(uint8_t is_write, uint64_t addr, uint8_t* buf, uint32_t size) {
    struct bridge_msg msg = {0};
    msg.is_write = is_write;
    msg.is_dma = 1;
    msg.addr = addr;
    msg.size = size;
    memcpy(msg.data, buf, size);
    return msg;
}

// Helper to extract uint32_t from data array
uint32_t get_data_u32(const struct bridge_msg *msg) {
    uint32_t val;
    memcpy(&val, msg->data, sizeof(val));
    return val;
}

int main(void)
{
    int sock;
    struct sockaddr_un addr = {0};
    const char *path = "/tmp/systemc.sock";
    sock = socket(AF_UNIX, SOCK_STREAM, 0);

    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
    connect(sock, (struct sockaddr *)&addr, sizeof(addr));

    uint8_t* tmp_buf = static_cast<uint8_t*>(calloc(DMA_BUFFER_SIZE, sizeof(uint8_t)));
    for(int i = 0; i < DMA_BUFFER_SIZE; ++i) {
        tmp_buf[i] = 2;
    }

    struct bridge_msg msgs[] = {
        // === Write 0xDEADBEEF to SRAM address 0x1000 ===
        make_msg(1, 0, REG_ADDR,    TEST_ADDR),
        make_msg(1, 0, REG_LEN,     4),
        make_msg(1, 0, REG_WDATA,   0xDEADBEEF),
        make_msg(1, 0, REG_CONTROL, CTRL_START | CTRL_WRITE),

        // === Write 0xCAFEBABE to SRAM address 0x1004 ===
        make_msg(1, 0, REG_ADDR,    TEST_ADDR + 4),
        make_msg(1, 0, REG_LEN,     4),
        make_msg(1, 0, REG_WDATA,   0xCAFEBABE),
        make_msg(1, 0, REG_CONTROL, CTRL_START | CTRL_WRITE),

        // === Write 0x12345678 to SRAM address 0x1008 ===
        make_msg(1, 0, REG_ADDR,    TEST_ADDR + 8),
        make_msg(1, 0, REG_LEN,     4),
        make_msg(1, 0, REG_WDATA,   0x12345678),
        make_msg(1, 0, REG_CONTROL, CTRL_START | CTRL_WRITE),

        // === Read back from SRAM address 0x1000 ===
        make_msg(1, 0, REG_ADDR,    TEST_ADDR),
        make_msg(1, 0, REG_LEN,     4),
        make_msg(1, 0, REG_CONTROL, CTRL_START),
        make_msg(0, 0, REG_RDATA,   0),  // expect 0xDEADBEEF

        // === Read back from SRAM address 0x1004 ===
        make_msg(1, 0, REG_ADDR,    TEST_ADDR + 4),
        make_msg(1, 0, REG_LEN,     4),
        make_msg(1, 0, REG_CONTROL, CTRL_START),
        make_msg(0, 0, REG_RDATA,   0),  // expect 0xCAFEBABE

        // === Read back from SRAM address 0x1008 ===
        make_msg(1, 0, REG_ADDR,    TEST_ADDR + 8),
        make_msg(1, 0, REG_LEN,     4),
        make_msg(1, 0, REG_CONTROL, CTRL_START),
        make_msg(0, 0, REG_RDATA,   0),  // expect 0x12345678

        // DMA test message
        make_dma_msg(1, WEIGHT_BASE_ADDR, tmp_buf, DMA_BUFFER_SIZE),

        // === Read back from SRAM address 0x1000 ===
        make_msg(1, 0, REG_ADDR,    TEST_ADDR),
        make_msg(1, 0, REG_LEN,     4),
        make_msg(1, 0, REG_CONTROL, CTRL_START),
        make_msg(0, 0, REG_RDATA,   0),  // expect DMA's message

        // === Read back from SRAM address 0x1004 ===
        make_msg(1, 0, REG_ADDR,    TEST_ADDR + 4),
        make_msg(1, 0, REG_LEN,     4),
        make_msg(1, 0, REG_CONTROL, CTRL_START),
        make_msg(0, 0, REG_RDATA,   0),  // expect DMA's message

        // === Read back from SRAM address 0x1008 ===
        make_msg(1, 0, REG_ADDR,    TEST_ADDR + 8),
        make_msg(1, 0, REG_LEN,     4),
        make_msg(1, 0, REG_CONTROL, CTRL_START),
        make_msg(0, 0, REG_RDATA,   0),  // expect DMA's message
    };

    struct bridge_msg response;
    int num_msgs = sizeof(msgs) / sizeof(msgs[0]);

    for (int i = 0; i < num_msgs; i++) {
        send(sock, &msgs[i], sizeof(struct bridge_msg), 0);

        // Wait for ACK/response
        recv(sock, &response, sizeof(struct bridge_msg), 0);

        printf("%s [0x%02lx] sent=0x%08x | ACK: status=%d is_write=%d is_dma=%d addr=0x%02lx data=0x%08x\n",
               msgs[i].is_write ? "WR" : "RD",
               (unsigned long)msgs[i].addr,
               get_data_u32(&msgs[i]),
               response.status,
               response.is_write,
               response.is_dma,
               (unsigned long)response.addr,
               get_data_u32(&response));    
    }

    close(sock);
    return 0;
}