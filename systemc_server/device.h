// Test device header

#pragma once
#include <cstdint>

class Device {
public:
    void write(uint64_t addr, uint64_t data, uint32_t size);
    uint64_t read(uint64_t addr, uint32_t size);
};
