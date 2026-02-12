#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "include/mem_controller_registers.h"

#define TEST_ADDR   0x1000

struct bridge_msg {
    uint8_t  is_write;
    uint64_t addr;
    uint32_t size;
    uint32_t data;
} __attribute__((packed));

int main(void)
{
    int sock;
    struct sockaddr_un addr = {0};
    const char *path = "/tmp/systemc.sock";
    sock = socket(AF_UNIX, SOCK_STREAM, 0);

    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
    connect(sock, (struct sockaddr *)&addr, sizeof(addr));

    struct bridge_msg msgs[] = {
        // === Write 0xDEADBEEF to SRAM address 0x1000 ===
        { .is_write = 1, .addr = REG_ADDR,    .size = 4, .data = TEST_ADDR },       // Set target address
        { .is_write = 1, .addr = REG_LEN,     .size = 4, .data = 4 },               // Set length
        { .is_write = 1, .addr = REG_WDATA,   .size = 4, .data = 0xDEADBEEF },      // Set write data
        { .is_write = 1, .addr = REG_CONTROL, .size = 4, .data = CTRL_START | CTRL_WRITE }, // Execute write

        // === Write 0xCAFEBABE to SRAM address 0x1004 ===
        { .is_write = 1, .addr = REG_ADDR,    .size = 4, .data = TEST_ADDR + 4 },
        { .is_write = 1, .addr = REG_LEN,     .size = 4, .data = 4 },
        { .is_write = 1, .addr = REG_WDATA,   .size = 4, .data = 0xCAFEBABE },
        { .is_write = 1, .addr = REG_CONTROL, .size = 4, .data = CTRL_START | CTRL_WRITE },

        // === Write 0x12345678 to SRAM address 0x1008 ===
        { .is_write = 1, .addr = REG_ADDR,    .size = 4, .data = TEST_ADDR + 8 },
        { .is_write = 1, .addr = REG_LEN,     .size = 4, .data = 4 },
        { .is_write = 1, .addr = REG_WDATA,   .size = 4, .data = 0x12345678 },
        { .is_write = 1, .addr = REG_CONTROL, .size = 4, .data = CTRL_START | CTRL_WRITE },

        // === Read back from SRAM address 0x1000 ===
        { .is_write = 1, .addr = REG_ADDR,    .size = 4, .data = TEST_ADDR },       // Set target address
        { .is_write = 1, .addr = REG_LEN,     .size = 4, .data = 4 },               // Set length
        { .is_write = 1, .addr = REG_CONTROL, .size = 4, .data = CTRL_START },      // Execute read (no CTRL_WRITE)
        { .is_write = 0, .addr = REG_RDATA,   .size = 4, .data = 0 },               // Read result -> expect 0xDEADBEEF

        // === Read back from SRAM address 0x1004 ===
        { .is_write = 1, .addr = REG_ADDR,    .size = 4, .data = TEST_ADDR + 4 },
        { .is_write = 1, .addr = REG_LEN,     .size = 4, .data = 4 },
        { .is_write = 1, .addr = REG_CONTROL, .size = 4, .data = CTRL_START },
        { .is_write = 0, .addr = REG_RDATA,   .size = 4, .data = 0 },               // expect 0xCAFEBABE

        // === Read back from SRAM address 0x1008 ===
        { .is_write = 1, .addr = REG_ADDR,    .size = 4, .data = TEST_ADDR + 8 },
        { .is_write = 1, .addr = REG_LEN,     .size = 4, .data = 4 },
        { .is_write = 1, .addr = REG_CONTROL, .size = 4, .data = CTRL_START },
        { .is_write = 0, .addr = REG_RDATA,   .size = 4, .data = 0 },               // expect 0x12345678
    };

    struct bridge_msg response;
    int num_msgs = sizeof(msgs) / sizeof(msgs[0]);

    for (int i = 0; i < num_msgs; i++) {
        send(sock, &msgs[i], sizeof(struct bridge_msg), 0);

        if (msgs[i].is_write) {
            printf("WR [0x%02lx] = 0x%08x\n",
                   (unsigned long)msgs[i].addr, msgs[i].data);
        } else {
            recv(sock, &response, sizeof(struct bridge_msg), 0);
            printf("RD [0x%02lx] = 0x%08x\n",
                   (unsigned long)msgs[i].addr, response.data);
        }
    }

    close(sock);
    return 0;
}