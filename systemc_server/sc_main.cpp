#include <systemc>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>
#include "device.h"

struct tlm_msg {
    uint8_t  is_write;
    uint64_t addr;
    uint32_t size;
    uint64_t data;
} __attribute__((packed));  /* Ensure no padding */

int sc_main(int argc, char *argv[])
{
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

    Device dev;

    /* Server loop */
    while (true) {
        tlm_msg msg{};
        recv(client_fd, &msg, sizeof(msg), 0);

        printf("[DEBUG] is_write: %d, addr: 0x%lx, data: 0x%lx, size: %d \n", msg.is_write, msg.addr, msg.data, msg.size);

        if (msg.is_write) {
            dev.write(msg.addr, msg.data, msg.size);
        } else {
            msg.data = dev.read(msg.addr, msg.size);

            printf("[DEBUG] Sending back: is_write: %d, addr: 0x%lx, data: 0x%lx, size: %d \n", msg.is_write, msg.addr, msg.data, msg.size);
        }

        send(client_fd, &msg, sizeof(msg), 0);
    }
}
