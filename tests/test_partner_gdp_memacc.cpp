#include "partner_gdp.hpp"

#include <cstdint>
#include <cstdio>

class partner_gdp_test_shim : public partner_gdp
{
public:
    using partner_gdp::partner_gdp;

    uint8_t read_port(uint16_t port)
    {
        return io_read(port);
    }

    void write_port(uint16_t port, uint8_t value)
    {
        io_write(port, value);
    }
};

static size_t page_bits(const ef9367_t &ef, int page)
{
    size_t bits = 0;
    for (uint8_t byte : ef.fb[page & 1]) {
        for (; byte != 0; byte &= (uint8_t)(byte - 1u)) {
            bits++;
        }
    }
    return bits;
}

static void draw_test_pixel(partner_gdp_test_shim &emu, uint16_t x, uint16_t y)
{
    emu.write_port(0x21, 0x03); // pen selected and down
    emu.write_port(0x28, (uint8_t)(x >> 8));
    emu.write_port(0x29, (uint8_t)x);
    emu.write_port(0x2A, (uint8_t)(y >> 8));
    emu.write_port(0x2B, (uint8_t)y);
    emu.write_port(0x20, 0x80); // zero-length small vector plots X,Y
}

int main()
{
    partner_gdp_test_shim emu(terminal_profile::vt100_ansi);
    emu.reset();

    emu.key_input('Q');
    for (size_t i = 0; i < 400; ++i)
        emu.tick();
    if (emu.pending_key_count() != 1u) {
        std::puts("test_partner_gdp_memacc: FAIL key lost while SIO RX disabled");
        return 1;
    }
    emu.reset();

    bool saw_sync_high = false;
    bool saw_sync_low = false;
    for (size_t i = 0; i < 256; i++) {
        emu.tick();
        if (emu.read_port(0x36) & 0x10u)
            saw_sync_high = true;
        else
            saw_sync_low = true;
    }
    if (!saw_sync_high || !saw_sync_low) {
        std::printf("test_partner_gdp_memacc: FAIL hsync high=%d low=%d\n",
                    saw_sync_high ? 1 : 0, saw_sync_low ? 1 : 0);
        return 1;
    }

    // AVDC vertical blank is a physical input to CTC channel 3. Program that
    // channel as a one-edge counter and verify the video chip can request its
    // interrupt without a board-level synthetic vector.
    emu.write_port(0xCB, Z80CTC_CTRL_EI | Z80CTC_CTRL_MODE_COUNTER |
                         Z80CTC_CTRL_EDGE_RISING |
                         Z80CTC_CTRL_CONST_FOLLOWS |
                         Z80CTC_CTRL_CONTROL);
    emu.write_port(0xCB, 1);
    bool saw_vblank_counter_irq = false;
    for (size_t i = 0; i < 60000; ++i) {
        emu.tick();
        if (emu.get_ctc().chn[3].int_state != 0) {
            saw_vblank_counter_irq = true;
            break;
        }
    }
    if (!saw_vblank_counter_irq) {
        std::puts("test_partner_gdp_memacc: FAIL AVDC VB did not clock CTC3");
        return 1;
    }

    size_t accepted = 0;
    for (size_t i = 0; i < 1024; i++) {
        if (emu.key_input('K'))
            accepted++;
    }
    const size_t pending = emu.pending_key_count();
    if (accepted > partner_gdp::KEY_FIFO_CAPACITY + 1u ||
        pending > partner_gdp::KEY_FIFO_CAPACITY + 1u ||
        accepted == 1024u)
    {
        std::printf("test_partner_gdp_memacc: FAIL keyboard accepted=%zu pending=%zu\n",
                    accepted, pending);
        return 1;
    }

    emu.reset();
    if (emu.pending_key_count() != 0u) {
        std::puts("test_partner_gdp_memacc: FAIL keyboard reset did not clear backlog");
        return 1;
    }

    // Program the GDP-local PIO exactly as the Partner ROM does, then verify
    // that EF clear-screen affects the selected write page only.
    emu.write_port(0x31, 0x07);
    emu.write_port(0x31, 0x0F);
    emu.write_port(0x30, 0x18); // 1024x512, display 0, write 0
    draw_test_pixel(emu, 100, 100);
    emu.write_port(0x30, 0x1A); // display 0, write 1
    draw_test_pixel(emu, 200, 200);

    if (page_bits(emu.get_ef9367(), 0) == 0u ||
        page_bits(emu.get_ef9367(), 1) == 0u) {
        std::puts("test_partner_gdp_memacc: FAIL did not draw on both GDP pages");
        return 1;
    }

    emu.write_port(0x20, 0x04);
    if (page_bits(emu.get_ef9367(), 0) == 0u ||
        page_bits(emu.get_ef9367(), 1) != 0u) {
        std::puts("test_partner_gdp_memacc: FAIL clear did not target write page 1 only");
        return 1;
    }

    emu.write_port(0x30, 0x18); // display 0, write 0
    emu.write_port(0x20, 0x04);
    if (page_bits(emu.get_ef9367(), 0) != 0u ||
        page_bits(emu.get_ef9367(), 1) != 0u) {
        std::puts("test_partner_gdp_memacc: FAIL both GDP pages were not cleared");
        return 1;
    }

    std::puts("test_partner_gdp_memacc: PASS");
    return 0;
}
