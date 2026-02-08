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

    // Debug control
    void enable_trace(bool enable) { trace_enabled = enable; }
    void enable_disassembly(bool enable) { disasm_enabled = enable; }

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

    // Debug control
    bool trace_enabled = false;
    bool disasm_enabled = false;
    uint16_t last_pc = 0;

    virtual uint8_t read_mem(uint16_t addr);
    virtual void write_mem(uint16_t addr, uint8_t data);
    virtual uint8_t io_read(uint16_t port);
    virtual void io_write(uint16_t port, uint8_t data);

    // Debug helpers
    void trace_instruction();
    void disassemble_instruction(uint16_t pc);
};
