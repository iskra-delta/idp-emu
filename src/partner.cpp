#include "partner.hpp"
#include <fstream>
#include <iostream>

partner::partner()
{
    z80sio_init(&sio);
    reset();
}

void partner::load_rom(const std::string &path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        throw std::runtime_error("cannot open rom file: " + path);
    file.read(reinterpret_cast<char *>(rom.data()), rom_size);
    if (!file)
        throw std::runtime_error("incomplete rom file: " + path);
    std::cout << "[info] rom loaded: " << path << "\n";
}

void partner::reset()
{
    pins = z80_reset(&cpu);
}

void partner::tick()
{
    pins = z80_tick(&cpu, pins);
    uint16_t addr = Z80_GET_ADDR(pins);

    if (pins & Z80_MREQ)
    {
        if (pins & Z80_RD)
        {
            Z80_SET_DATA(pins, read_mem(addr));
        }
        else if (pins & Z80_WR)
        {
            write_mem(addr, Z80_GET_DATA(pins));
        }
    }
    else if (pins & Z80_IORQ)
    {
        if (pins & Z80_RD)
        {
            Z80_SET_DATA(pins, io_read(addr));
        }
        else if (pins & Z80_WR)
        {
            io_write(addr, Z80_GET_DATA(pins));
        }
    }
}

uint8_t partner::read_mem(uint16_t addr)
{
    if (addr < rom_size)
        return rom[addr];
    return ram[addr];
}

void partner::write_mem(uint16_t addr, uint8_t data)
{
    if (addr >= rom_size)
        ram[addr] = data;
}

uint8_t partner::io_read(uint16_t port)
{
    // no implementation in base
    return 0xFF;
}

void partner::io_write(uint16_t port, uint8_t data)
{
    // no implementation in base
}
