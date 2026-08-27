#include "partner.hpp"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <vector>

class partner_board_test : public partner
{
public:
    explicit partner_board_test(const std::string &nvram) : partner(nvram) {}

    using partner::io_read;
    using partner::io_write;

    uint64_t daisy(uint64_t bus)
    {
        return clock_zilog_daisy_chain(bus);
    }

    void request_ctc(uint8_t vector)
    {
        ctc.chn[0].int_vector = vector;
        ctc.chn[0].int_state = Z80CTC_INT_NEEDED;
    }

    void request_dma(uint8_t vector)
    {
        dma.int_vector = vector;
        dma.int_state = Z80DMA_INT_NEEDED;
    }

    void request_fdc(uint8_t vector)
    {
        fdc_int_vector = vector;
        fdc.irq_request = true;
        fdc_irq_level_ = false;
    }

    uint8_t sio1_wr(int channel, int reg) const { return sio.chn[channel].wr[reg]; }
    uint8_t sio2_wr(int channel, int reg) const { return sio2.chn[channel].wr[reg]; }
    uint8_t ctc_vector() const { return ctc.chn[0].int_vector; }
};

int main()
{
    int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

    namespace fs = std::filesystem;
    const fs::path nvram = fs::path(IDP_SOURCE_ROOT) / "tests/dump/partner-board.nvram";
    std::error_code ec;
    fs::remove(nvram, ec);
    partner_board_test emu(nvram.string());

    std::vector<uint8_t> rom(partner::rom_size, 0x11);
    rom.front() = 0xA5;
    rom.back() = 0x5A;
    emu.load_debug_rom(rom);
    CHECK(emu.peek_mem(0x0000) == 0xA5);
    CHECK(emu.peek_mem(0x07FF) == 0x5A);
    CHECK(emu.peek_mem(0x0800) == 0xFF);
    CHECK(emu.peek_mem(0x1000) == 0xFF);

    rom.resize(partner::rom_capacity, 0x22);
    emu.load_debug_rom(rom);
    CHECK(emu.peek_mem(0x0000) == 0xA5);
    CHECK(emu.peek_mem(0x0800) == 0x22);
    CHECK(emu.peek_mem(0x0FFF) == 0x22);
    CHECK(emu.peek_mem(0x1000) == 0xFF);

    // Incompletely decoded motherboard selects expose these exact mirrors.
    emu.io_write(0xCC, 0xE0);
    CHECK(emu.ctc_vector() == 0xE0);
    emu.io_write(0xD1, 0x0F); // PIO A, output mode
    emu.io_write(0xD4, 0x5A); // mirror of D0
    CHECK(emu.io_read(0xD0) == 0x5A);

    emu.io_write(0xD9, 0x01);
    emu.io_write(0xDD, 0x18); // mirror of D9, WR1 value
    CHECK(emu.sio1_wr(Z80SIO_CHANNEL_A, 1) == 0x18);
    emu.io_write(0xE1, 0x01);
    emu.io_write(0xE5, 0x19); // mirror of E1, WR1 value
    CHECK(emu.sio2_wr(Z80SIO_CHANNEL_A, 1) == 0x19);

    emu.io_write(0xEF, 0xD6);
    CHECK(emu.get_fdc_int_vector() == 0xD6);
    CHECK(emu.io_read(0xF0) == emu.io_read(0xF2));
    CHECK(emu.io_read(0xF4) == emu.io_read(0xF6));
    emu.io_write(0x9F, 0);
    CHECK(emu.io_read(0x98) == 1);
    CHECK(emu.io_read(0x9E) == 1);
    CHECK(emu.io_read(0x10) == emu.io_read(0x14));
    CHECK(emu.io_read(0x18) == emu.io_read(0x1C));
    CHECK(emu.io_read(0x12) == 0xFF);
    CHECK(emu.io_read(0x16) == 0xFF);
    CHECK(emu.io_read(0x1A) == 0xFF);
    CHECK(emu.io_read(0x1E) == 0xFF);

    emu.io_write(0xC7, 0xC3); // every C0h..C7h address selects the DMA
    CHECK(emu.get_dma().wr[6] == 0x00); // RESET clears the register image
    CHECK(!emu.get_dma().enabled);

    // The schematic priority is CTC before DMA.
    emu.reset();
    emu.request_ctc(0x88);
    emu.request_dma(0x9A);
    uint64_t bus = emu.daisy(0);
    CHECK((bus & Z80_INT) != 0);
    bus = emu.daisy(Z80_M1 | Z80_IORQ);
    CHECK(Z80_GET_DATA(bus) == 0x88);
    emu.daisy(Z80_RETI);
    bus = emu.daisy(0);
    CHECK((bus & Z80_INT) != 0);
    bus = emu.daisy(Z80_M1 | Z80_IORQ);
    CHECK(Z80_GET_DATA(bus) == 0x9A);
    emu.daisy(Z80_RETI);

    // Discrete FDC logic holds its request, supplies the E8h vector on IACK,
    // blocks downstream IEI while serviced, and releases only on RETI.
    emu.request_fdc(0xD6);
    bus = emu.daisy(0);
    CHECK((bus & Z80_INT) != 0);
    CHECK((bus & Z80_IEIO) == 0);
    bus = emu.daisy(Z80_M1 | Z80_IORQ);
    CHECK(Z80_GET_DATA(bus) == 0xD6);
    CHECK((emu.get_fdc_int_state() & 0x04u) != 0u);
    bus = emu.daisy(0);
    CHECK((bus & Z80_IEIO) == 0);
    bus = emu.daisy(Z80_RETI);
    CHECK((emu.get_fdc_int_state() & 0x04u) == 0u);
    CHECK((bus & Z80_IEIO) != 0);

    // The motherboard directly wires E67 ZC/TO0 (pin 7) to CLK/TRG1
    // (pin 22). With both channels counting one rising edge, the first XX1
    // edge must therefore propagate through channel 1 and clear the motor
    // latch through ZC/TO1 on the following system clock.
    std::vector<uint8_t> halt_rom(partner::rom_size, 0x76);
    emu.load_debug_rom(halt_rom);
    emu.reset();
    emu.io_write(0xC8, 0x57);
    emu.io_write(0xC8, 0x01);
    emu.io_write(0xC9, 0x57);
    emu.io_write(0xC9, 0x01);
    emu.io_write(0x98, 0x01);
    CHECK((emu.io_read(0x98) & 0x01u) != 0u);
    for (int clock = 0; clock < 1300; ++clock)
        emu.tick();
    CHECK((emu.io_read(0x98) & 0x01u) == 0u);

    fs::remove(nvram, ec);
#undef CHECK
    if (failures == 0) {
        std::puts("test_partner_board: all tests passed");
        return 0;
    }
    return 1;
}
