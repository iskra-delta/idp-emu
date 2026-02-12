#pragma once
#include "z80.h"
#include "z80sio.h"
#include "z80pio.h"
#include "z80ctc.h"
#include "z80dma.h"
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

    // State accessors for GUI panels (read-only)
    const z80_t& get_cpu() const { return cpu; }
    const z80dma_t& get_dma() const { return dma; }
    const z80ctc_t& get_ctc() const { return ctc; }
    const z80sio_t& get_sio() const { return sio; }
    const z80pio_t& get_pio() const { return pio; }
    uint8_t peek_mem(uint16_t addr) const {
        if (rom_enabled && addr < rom_size) return rom[addr];
        return ram[addr];
    }
    uint64_t get_tick_count() const { return tick_count; }
    bool is_rom_enabled() const { return rom_enabled; }
    uint8_t get_ram_bank() const { return ram_bank; }

protected:
    // All Zilog chips in Partner system
    z80_t cpu{};
    z80dma_t dma{};    // Highest priority (if present)
    z80ctc_t ctc{};    // Second priority
    z80sio_t sio{};    // Third priority
    z80pio_t pio{};    // Lowest priority

    uint64_t pins = 0;
    uint64_t tick_count = 0;

    std::array<uint8_t, rom_size> rom{};
    std::array<uint8_t, ram_size> ram{};

    // Banking control
    bool rom_enabled = true;
    uint8_t ram_bank = 1;  // Bank 1 is default

    virtual uint8_t read_mem(uint16_t addr);
    virtual void write_mem(uint16_t addr, uint8_t data);
    virtual uint8_t io_read(uint16_t port);
    virtual void io_write(uint16_t port, uint8_t data);
};
