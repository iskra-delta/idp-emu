#include "partner.hpp"
#include <fstream>
#include <iostream>
#include <iomanip>

partner::partner()
{
    // Initialize all Zilog chips
    z80dma_init(&dma);
    z80ctc_init(&ctc);
    z80sio_init(&sio);
    z80pio_init(&pio);

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
    z80dma_reset(&dma);
    z80ctc_reset(&ctc);
    z80sio_reset(&sio);
    z80pio_reset(&pio);

    rom_enabled = true;
    ram_bank = 1;
    tick_count = 0;
    last_pc = 0;
}

void partner::tick()
{
    tick_count++;

    // Trace instruction if enabled (on M1 cycle)
    if ((trace_enabled || disasm_enabled) && (pins & Z80_M1))
    {
        trace_instruction();
    }

    // Tick the CPU
    pins = z80_tick(&cpu, pins);
    uint16_t addr = Z80_GET_ADDR(pins);

    // Handle memory requests
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
    // Handle I/O requests
    else if (pins & Z80_IORQ)
    {
        if (pins & Z80_RD)
        {
            Z80_SET_DATA(pins, io_read(addr & 0xFF));
        }
        else if (pins & Z80_WR)
        {
            io_write(addr & 0xFF, Z80_GET_DATA(pins));
        }
    }

    // Tick peripheral chips in interrupt daisy chain order
    // Set IEIO to enable interrupt daisy chain
    pins |= Z80_IEIO;

    // Highest priority: DMA (port 0xC0)
    if (addr == 0xC0 && (pins & Z80_IORQ))
    {
        pins |= Z80DMA_CE;
    }
    pins = z80dma_tick(&dma, pins);
    pins &= ~Z80DMA_CE;

    // Second priority: CTC (not explicitly mapped in Partner docs, but standard)
    // CTC would be here if present
    pins = z80ctc_tick(&ctc, pins);

    // Third priority: SIO (ports 0xD8-0xDB for Ch1, 0xE0-0xE4 for Ch2)
    // Note: The SIO uses CE pin to know when it's being addressed
    uint8_t port = addr & 0xFF;
    if ((port >= 0xD8 && port <= 0xDB) || (port >= 0xE0 && port <= 0xE4))
    {
        pins |= Z80SIO_CE;
    }
    pins = z80sio_tick(&sio, pins);
    pins &= ~Z80SIO_CE;

    // Lowest priority: PIO (ports 0xD0-0xD3)
    if (port >= 0xD0 && port <= 0xD3)
    {
        pins |= Z80PIO_CE;
    }
    pins = z80pio_tick(&pio, pins);
    pins &= ~Z80PIO_CE;
}

uint8_t partner::read_mem(uint16_t addr)
{
    // ROM at 0x0000-0x07FF (can be disabled)
    if (rom_enabled && addr < rom_size)
    {
        return rom[addr];
    }

    // RAM (with banking)
    return ram[addr];
}

void partner::write_mem(uint16_t addr, uint8_t data)
{
    // Can't write to ROM area when ROM is enabled
    if (rom_enabled && addr < rom_size)
        return;

    ram[addr] = data;
}

uint8_t partner::io_read(uint16_t port)
{
    port &= 0xFF;

    // Z80 PIO: 0xD0-0xD3
    if (port >= 0xD0 && port <= 0xD3)
    {
        // PIO handles its own I/O in tick function via CE pin
        return 0xFF;
    }

    // Z80 SIO Channel 1: 0xD8-0xDB
    // Z80 SIO Channel 2: 0xE0-0xE4
    if ((port >= 0xD8 && port <= 0xDB) || (port >= 0xE0 && port <= 0xE4))
    {
        // SIO handles its own I/O in tick function via CE pin
        return 0xFF;
    }

    // Z80 DMA: 0xC0
    if (port == 0xC0)
    {
        return z80dma_read(&dma);
    }

    // Banking and control: 0x80, 0x88, 0x90
    if (port == 0x80)
    {
        // Reading 0x80 returns ROM enabled status
        return rom_enabled ? 0x01 : 0x00;
    }

    return 0xFF;
}

void partner::io_write(uint16_t port, uint8_t data)
{
    port &= 0xFF;

    // Z80 PIO: 0xD0-0xD3
    if (port >= 0xD0 && port <= 0xD3)
    {
        // PIO handles its own I/O in tick function via CE pin
        return;
    }

    // Z80 SIO Channel 1: 0xD8-0xDB
    // Z80 SIO Channel 2: 0xE0-0xE4
    if ((port >= 0xD8 && port <= 0xDB) || (port >= 0xE0 && port <= 0xE4))
    {
        // SIO handles its own I/O in tick function via CE pin
        return;
    }

    // Z80 DMA: 0xC0
    if (port == 0xC0)
    {
        z80dma_write(&dma, data);
        return;
    }

    // Memory banking and ROM control
    if (port == 0x80)
    {
        // Disable ROM (enable RAM at 0x0000-0x07FF)
        rom_enabled = false;
        if (trace_enabled)
            std::cout << "[bank] ROM disabled, RAM visible at 0x0000\n";
        return;
    }

    if (port == 0x88)
    {
        // Select RAM Bank 1 (default)
        ram_bank = 1;
        if (trace_enabled)
            std::cout << "[bank] RAM Bank 1 selected\n";
        return;
    }

    if (port == 0x90)
    {
        // Select RAM Bank 2
        ram_bank = 2;
        if (trace_enabled)
            std::cout << "[bank] RAM Bank 2 selected\n";
        return;
    }
}

void partner::trace_instruction()
{
    uint16_t pc = cpu.pc;

    // Only trace on new instruction (PC changed)
    if (pc == last_pc)
        return;

    last_pc = pc;

    if (trace_enabled)
    {
        std::cout << "[" << std::dec << std::setw(10) << tick_count << "] "
                  << "PC=" << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << pc
                  << " A=" << std::setw(2) << (int)cpu.a
                  << " BC=" << std::setw(4) << cpu.bc
                  << " DE=" << std::setw(4) << cpu.de
                  << " HL=" << std::setw(4) << cpu.hl
                  << " SP=" << std::setw(4) << cpu.sp
                  << " F=" << std::setw(2) << (int)cpu.f;

        if (disasm_enabled)
        {
            std::cout << " | ";
            disassemble_instruction(pc);
        }

        std::cout << "\n" << std::dec;
    }
    else if (disasm_enabled)
    {
        std::cout << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << pc << ": ";
        disassemble_instruction(pc);
        std::cout << "\n" << std::dec;
    }
}

void partner::disassemble_instruction(uint16_t pc)
{
    // Read opcode from memory
    uint8_t op = read_mem(pc);

    // Simple disassembler for common opcodes
    std::cout << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << (int)op << " ";

    switch (op)
    {
    case 0x00: std::cout << "NOP"; break;
    case 0x76: std::cout << "HALT"; break;
    case 0xC3:
        std::cout << "JP " << std::setw(4) << (read_mem(pc+2) << 8 | read_mem(pc+1));
        break;
    case 0xCD:
        std::cout << "CALL " << std::setw(4) << (read_mem(pc+2) << 8 | read_mem(pc+1));
        break;
    case 0xC9: std::cout << "RET"; break;
    case 0x3E:
        std::cout << "LD A," << std::setw(2) << (int)read_mem(pc+1);
        break;
    case 0x06:
        std::cout << "LD B," << std::setw(2) << (int)read_mem(pc+1);
        break;
    case 0x0E:
        std::cout << "LD C," << std::setw(2) << (int)read_mem(pc+1);
        break;
    case 0x16:
        std::cout << "LD D," << std::setw(2) << (int)read_mem(pc+1);
        break;
    case 0x1E:
        std::cout << "LD E," << std::setw(2) << (int)read_mem(pc+1);
        break;
    case 0x26:
        std::cout << "LD H," << std::setw(2) << (int)read_mem(pc+1);
        break;
    case 0x2E:
        std::cout << "LD L," << std::setw(2) << (int)read_mem(pc+1);
        break;
    case 0x21:
        std::cout << "LD HL," << std::setw(4) << (read_mem(pc+2) << 8 | read_mem(pc+1));
        break;
    case 0x31:
        std::cout << "LD SP," << std::setw(4) << (read_mem(pc+2) << 8 | read_mem(pc+1));
        break;
    case 0xD3:
        std::cout << "OUT (" << std::setw(2) << (int)read_mem(pc+1) << "),A";
        break;
    case 0xDB:
        std::cout << "IN A,(" << std::setw(2) << (int)read_mem(pc+1) << ")";
        break;
    case 0xED:
    {
        uint8_t op2 = read_mem(pc+1);
        std::cout << std::setw(2) << (int)op2 << " ";
        if (op2 == 0xB0) std::cout << "LDIR";
        else if (op2 == 0xA0) std::cout << "LDI";
        else std::cout << "[ED prefix]";
        break;
    }
    default:
        std::cout << "[...]";
        break;
    }
}