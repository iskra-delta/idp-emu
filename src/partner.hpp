#pragma once
#include "z80.h"
#include "z80sio.h"
#include <array>
#include <string>
#include <cstdint>

class partner
{
public:
    static constexpr size_t rom_size = 0x0800;
    static constexpr size_t ram_size = 0x10000;

    partner();
    virtual ~partner() = default;

    void load_rom(const std::string &path);
    virtual void reset();
    virtual void tick();

protected:
    z80_t cpu{};
    z80sio_t sio{};
    uint64_t pins = 0;

    std::array<uint8_t, rom_size> rom{};
    std::array<uint8_t, ram_size> ram{};

    virtual uint8_t read_mem(uint16_t addr);
    virtual void write_mem(uint16_t addr, uint8_t data);
    virtual uint8_t io_read(uint16_t port);
    virtual void io_write(uint16_t port, uint8_t data);
};
