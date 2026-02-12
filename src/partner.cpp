#include "partner.hpp"
#include <fstream>
#include <iostream>

partner::partner()
{
    // Initialize all chips
    z80dma_init(&dma);
    z80ctc_init(&ctc);
    z80sio_init(&sio);
    z80pio_init(&pio);
    i8272_init(&fdc);
    mm58167a_init(&rtc);

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
    i8272_reset(&fdc);
    mm58167a_reset(&rtc);

    rom_enabled = true;
    ram_bank = 1;
    fdc_int_vector = 0;
    fdc_motor = 0;
    tick_count = 0;
}

void partner::tick()
{
    tick_count++;
    mm58167a_sync_time(&rtc);

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

    // MM58167 RTC: 0xA0-0xBF
    if ((port >= 0xA0 && port <= 0xB6) || port == 0xBF)
    {
        return rtc.regs[port - 0xA0];
    }

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

    // Intel 8272 FDC Status: 0xF0
    if (port == 0xF0)
    {
        return i8272_read_status(&fdc);
    }

    // Intel 8272 FDC Data: 0xF1
    if (port == 0xF1)
    {
        return i8272_read_data(&fdc);
    }

    // FDC Motor Status: 0x98
    if (port == 0x98)
    {
        return fdc_motor;
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

    // MM58167 RTC: 0xA0-0xBF
    if ((port >= 0xA0 && port <= 0xB6) || port == 0xBF)
    {
        rtc.regs[port - 0xA0] = data;
        // Writing reset-counter register refreshes visible time registers.
        if (port == 0xB2)
            mm58167a_sync_time(&rtc);
        return;
    }

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

    // Intel 8272 FDC Data: 0xF1
    if (port == 0xF1)
    {
        i8272_write_data(&fdc, data);
        return;
    }

    // FDC Motor Control: 0x98
    if (port == 0x98)
    {
        fdc_motor = data;
        return;
    }

    // FDC Interrupt Vector: 0xE8
    if (port == 0xE8)
    {
        fdc_int_vector = data;
        return;
    }

    // Memory banking and ROM control
    if (port == 0x80)
    {
        rom_enabled = false;
        return;
    }

    if (port == 0x88)
    {
        ram_bank = 1;
        return;
    }

    if (port == 0x90)
    {
        ram_bank = 2;
        return;
    }
}
