#include "device.h"
#include <cstdio>

static uint32_t reg0 = 0xCAFEBEEF; // 32 bit register storing data

void Device::write(uint64_t addr, uint64_t data, uint32_t)
{
    printf("[SystemC] WRITE addr=0x%lx data=0x%lx\n", addr, data);
    reg0 = data;
}

uint64_t Device::read(uint64_t addr, uint32_t)
{
    printf("[SystemC] READ addr=0x%lx\n", addr);
    return reg0;
}
