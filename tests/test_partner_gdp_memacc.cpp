#include "partner_gdp.hpp"
#include "gui/display.hpp"

#include <cstdint>
#include <cstdio>
#include <memory>

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

    scn2674_t &mutable_avdc()
    {
        return const_cast<scn2674_t &>(get_avdc());
    }
};

static size_t frame_pixels(const display &frame)
{
    size_t pixels = 0;
    const uint8_t *data = frame.data();
    for (size_t i = 0; i < (size_t)display::FB_W * display::FB_H; ++i) {
        if (data[i] != 0u)
            pixels++;
    }
    return pixels;
}

static void configure_test_text_raster(partner_gdp_test_shim &emu)
{
    // PIO B: mode 0 outputs, monochrome, 8 dots/character at 18 MHz.
    emu.write_port(0x33, 0x07);
    emu.write_port(0x33, 0x0F);
    emu.write_port(0x32, 0x44);

    scn2674_t &avdc = emu.mutable_avdc();
    avdc.rows_per_screen = 1u;
    avdc.chars_per_row = 2u;
    avdc.scanlines_per_char_row = 1u;
    avdc.scanlines_per_field_row = 1u;
    avdc.start1_addr = 0u;
    avdc.display_buffer_first_addr = 0u;
    avdc.display_buffer_last_addr = 0x3FFFu;
    avdc.use_row_table = false;
    avdc.gfx_enabled = false;
    avdc.interlace_enabled = false;
    avdc.cursor_enabled = false;
}

static size_t render_test_frame(partner_gdp_test_shim &emu)
{
    auto frame = std::make_unique<display>();
    emu.render_to(*frame);
    return frame_pixels(*frame);
}

static bool test_user_defined_characters_and_dot_stretch()
{
    partner_gdp_test_shim emu(terminal_profile::vt100_ansi);
    emu.reset();
    configure_test_text_raster(emu);
    scn2674_t &avdc = emu.mutable_avdc();

    // Attribute bit 2 selects the Partner's 2 KiB character RAM.  Each of
    // the 128 UDGs occupies sixteen bytes at 0x2000 + char*16 + line.
    avdc.vram[0] = 0x21u;
    avdc.vram[1] = 0x22u;
    avdc.attr_vram[0] = 0x04u;
    avdc.attr_vram[1] = 0x04u;
    avdc.vram[0x2000u + 0x21u * 16u] = 0x02u;
    avdc.vram[0x2000u + 0x22u * 16u] = 0x02u;
    const size_t normal_pixels = render_test_frame(emu);
    if (normal_pixels == 0u) {
        std::puts("test_partner_gdp_memacc: FAIL UDG character RAM was not rendered");
        return false;
    }

    // DOTS is ATTD3 sampled at falling BLANK.  Setting bit 3 on the first
    // fetch therefore stretches every character on that scan line.
    avdc.attr_vram[0] = 0x0Cu;
    avdc.attr_vram[1] = 0x04u;
    const size_t stretched_pixels = render_test_frame(emu);
    if (stretched_pixels <= normal_pixels) {
        std::puts("test_partner_gdp_memacc: FAIL scan-line dot stretch had no effect");
        return false;
    }

    // Bit 3 on a later character is not sampled as a per-character attribute.
    avdc.attr_vram[0] = 0x04u;
    avdc.attr_vram[1] = 0x0Cu;
    const size_t later_attribute_pixels = render_test_frame(emu);
    if (later_attribute_pixels != normal_pixels) {
        std::puts("test_partner_gdp_memacc: FAIL dot stretch was applied per character");
        return false;
    }
    return true;
}

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

static bool gdp_wait_ready_like_cgraf(partner_gdp_test_shim &emu)
{
    size_t guard = 1000u;
    while ((emu.read_port(0x2F) & 0x04u) == 0u && guard-- != 0u)
        emu.tick();
    return guard != 0u;
}

static bool read_gdp_pixel_like_cgraf(partner_gdp_test_shim &emu,
                                      uint16_t x, uint16_t y,
                                      uint8_t &board_value)
{
    if (!gdp_wait_ready_like_cgraf(emu))
        return false;
    emu.write_port(0x28, (uint8_t)(x >> 8));
    emu.write_port(0x29, (uint8_t)x);
    emu.write_port(0x2A, (uint8_t)(y >> 8));
    emu.write_port(0x2B, (uint8_t)y);
    emu.write_port(0x20, 0x0F);
    if ((emu.read_port(0x2F) & 0x04u) != 0u)
        return false;
    if (!gdp_wait_ready_like_cgraf(emu))
        return false;
    board_value = emu.read_port(0x36);
    return true;
}

static bool test_gdp_pixel_read_latch()
{
    partner_gdp_test_shim emu(terminal_profile::vt100_ansi);
    emu.reset();

    /* Program GDP-local PIO A as the ROM does: 512-line format, read page 0,
       write page 0. The IC1 latch powers up to the inactive/high sense. */
    emu.write_port(0x31, 0x07);
    emu.write_port(0x31, 0x0F);
    emu.write_port(0x30, 0x18);
    if ((emu.read_port(0x36) & 0x80u) == 0u) {
        std::puts("test_partner_gdp_memacc: FAIL pixel latch reset polarity");
        return false;
    }

    draw_test_pixel(emu, 100u, 100u);
    uint8_t value = 0u;
    if (!read_gdp_pixel_like_cgraf(emu, 100u, 100u, value) ||
        (value & 0x80u) != 0u) {
        std::puts("test_partner_gdp_memacc: FAIL set GDP pixel did not drive active-low D7");
        return false;
    }

    /* IC1 retains the sampled value until another 0Fh access completes. */
    for (int i = 0; i < 32; ++i)
        emu.tick();
    if ((emu.read_port(0x36) & 0x80u) != 0u) {
        std::puts("test_partner_gdp_memacc: FAIL GDP pixel latch did not retain D7");
        return false;
    }

    if (!read_gdp_pixel_like_cgraf(emu, 101u, 100u, value) ||
        (value & 0x80u) == 0u) {
        std::puts("test_partner_gdp_memacc: FAIL clear GDP pixel did not release active-low D7");
        return false;
    }

    /* Direct access follows WBNK. Page 0 contains the set pixel, page 1 is
       still clear even though RBNK continues to display page 0. */
    emu.write_port(0x30, 0x1A);
    if (!read_gdp_pixel_like_cgraf(emu, 100u, 100u, value) ||
        (value & 0x80u) == 0u) {
        std::puts("test_partner_gdp_memacc: FAIL direct pixel read ignored WBNK");
        return false;
    }
    emu.write_port(0x30, 0x18);
    if (!read_gdp_pixel_like_cgraf(emu, 100u, 100u, value) ||
        (value & 0x80u) != 0u) {
        std::puts("test_partner_gdp_memacc: FAIL direct pixel read did not return page 0");
        return false;
    }
    return true;
}

static bool avdc_wait_access_like_hardware(partner_gdp_test_shim &emu)
{
    size_t guard = 200000u;
    while ((emu.read_port(0x36) & 0x10u) == 0u && guard-- != 0u)
        emu.tick();
    while ((emu.read_port(0x36) & 0x10u) != 0u && guard-- != 0u)
        emu.tick();
    return guard != 0u;
}

static bool avdc_wait_ready_like_hardware(partner_gdp_test_shim &emu)
{
    size_t guard = 200000u;
    while ((emu.read_port(0x39) & 0x20u) == 0u && guard-- != 0u)
        emu.tick();
    return guard != 0u;
}

static void configure_ultimate_avdc(partner_gdp_test_shim &emu, bool columns132)
{
    static const uint8_t init80[] = {
        0xD0, 0x2F, 0x8D, 0x05, 0x99, 0x4F, 0x0A, 0xEA, 0x00, 0x30
    };
    static const uint8_t init132[] = {
        0xD0, 0x3E, 0xBF, 0x05, 0x99, 0x83, 0x0B, 0xEA, 0x00, 0x30
    };
    const uint8_t *init = columns132 ? init132 : init80;

    emu.reset();
    emu.write_port(0x33, 0x07);
    emu.write_port(0x33, 0x0F); /* PIO B mode 0, all outputs */
    emu.write_port(0x32, columns132 ? 0xC4 : 0x65);
    emu.write_port(0x39, 0x00);
    emu.write_port(0x39, 0x00); /* physical power-up sequence uses two resets */
    emu.write_port(0x3E, 0x00);
    emu.write_port(0x3F, 0x00);
    emu.write_port(0x39, 0x10);
    for (uint8_t i = 0; i < 10u; ++i)
        emu.write_port(0x38, init[i]);
    emu.write_port(0x3E, 0x00);
    emu.write_port(0x3F, 0x00);
    emu.write_port(0x39, 0x3D); /* display and cursor on at next field */
}

static bool avdc_write_at_pointer_like_hardware(partner_gdp_test_shim &emu,
                                                 uint16_t addr,
                                                 uint8_t chr,
                                                 uint8_t attr)
{
    if (!avdc_wait_access_like_hardware(emu) ||
        !avdc_wait_ready_like_hardware(emu)) {
        return false;
    }
    emu.write_port(0x39, 0x1A); /* select display-pointer registers */
    emu.write_port(0x38, (uint8_t)addr);
    emu.write_port(0x38, (uint8_t)((addr >> 8) & 0x3Fu));
    emu.write_port(0x34, chr);
    emu.write_port(0x35, attr);
    emu.write_port(0x39, 0xA2); /* write at display pointer */
    return avdc_wait_ready_like_hardware(emu);
}

static bool test_user_defined_character_port_protocol()
{
    static const uint8_t glyph[16] = {
        0x3C, 0x42, 0xA5, 0x81, 0xA5, 0x99, 0x42, 0x3C,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    constexpr uint8_t udg_code = 0x21u;
    constexpr uint16_t udg_addr = 0x2000u + udg_code * 16u;
    constexpr uint16_t screen_addr = 0x0100u;

    partner_gdp_test_shim emu(terminal_profile::vt100_ansi);
    configure_ultimate_avdc(emu, false);
    emu.tick();

    for (uint16_t line = 0; line < 16u; ++line) {
        if (!avdc_write_at_pointer_like_hardware(
                emu, (uint16_t)(udg_addr + line), glyph[line], 0u)) {
            std::puts("test_partner_gdp_memacc: FAIL UDG pointer write timed out");
            return false;
        }
    }

    if (!avdc_wait_access_like_hardware(emu) ||
        !avdc_wait_ready_like_hardware(emu)) {
        std::puts("test_partner_gdp_memacc: FAIL UDG cursor write timed out");
        return false;
    }
    emu.write_port(0x3C, (uint8_t)screen_addr);
    emu.write_port(0x3D, (uint8_t)(screen_addr >> 8));
    emu.write_port(0x34, udg_code);
    emu.write_port(0x35, 0x04); /* ATTD2 selects writable character RAM */
    emu.write_port(0x39, 0xAA); /* write at cursor without increment */
    if (!avdc_wait_ready_like_hardware(emu)) {
        std::puts("test_partner_gdp_memacc: FAIL UDG cursor command timed out");
        return false;
    }

    const scn2674_t &avdc = emu.get_avdc();
    for (uint16_t line = 0; line < 16u; ++line) {
        if (avdc.vram[udg_addr + line] != glyph[line] ||
            avdc.attr_vram[udg_addr + line] != 0u) {
            std::puts("test_partner_gdp_memacc: FAIL UDG data address/protocol");
            return false;
        }
    }
    if (avdc.vram[screen_addr] != udg_code ||
        avdc.attr_vram[screen_addr] != 0x04u ||
        avdc.cursor_addr != screen_addr) {
        std::puts("test_partner_gdp_memacc: FAIL UDG screen-cell protocol");
        return false;
    }
    return true;
}

static bool test_ultimate_avdc_access_and_ready(bool columns132)
{
    partner_gdp_test_shim emu(terminal_profile::vt100_ansi);
    configure_ultimate_avdc(emu, columns132);
    emu.tick(); /* propagate the programmed PIO clock/divider to the AVDC */
    const scn2674_t &avdc = emu.get_avdc();
    const uint16_t expected_line = columns132 ? 190u : 112u;
    const uint8_t expected_width = columns132 ? 8u : 9u;
    const uint32_t expected_dot_clock = columns132 ? 24000000u : 18000000u;
    if (avdc.scanlines_per_field_row != 11u || avdc.rows_per_screen != 26u ||
        avdc.chars_per_row != (columns132 ? 132u : 80u) ||
        avdc.dots_per_character != expected_width ||
        avdc.dot_clock_hz != expected_dot_clock ||
        (uint16_t)(2u * (avdc.equalizing_constant + 2u * avdc.hsync_width)) != expected_line) {
        std::printf("test_partner_gdp_memacc: FAIL ultimate %u-column decode\n",
                    columns132 ? 132u : 80u);
        return false;
    }

    /* avdc_wait_access() waits for RESTRICT high then low, returning at line
       zero.  The low interval must cover the ten safe lines before LL is
       latched high for the eleventh (last) line. */
    if (!avdc_wait_access_like_hardware(emu)) {
        std::printf("test_partner_gdp_memacc: FAIL ultimate %u-column access wait\n",
                    columns132 ? 132u : 80u);
        return false;
    }
    size_t safe_master_ticks = 0u;
    while ((emu.read_port(0x36) & 0x10u) == 0u && safe_master_ticks < 5000u) {
        emu.tick();
        safe_master_ticks++;
    }
    const size_t safe_min = columns132 ? 2533u : 2240u;
    const size_t safe_max = columns132 ? 2534u : 2240u;
    if (safe_master_ticks < safe_min || safe_master_ticks > safe_max) {
        std::printf("test_partner_gdp_memacc: FAIL ultimate %u-column safe ticks=%zu\n",
                    columns132 ? 132u : 80u, safe_master_ticks);
        return false;
    }

    size_t restricted_master_ticks = 0u;
    while ((emu.read_port(0x36) & 0x10u) != 0u && restricted_master_ticks < 1000u) {
        emu.tick();
        restricted_master_ticks++;
    }
    const size_t restrict_min = columns132 ? 253u : 224u;
    const size_t restrict_max = columns132 ? 254u : 224u;
    if (restricted_master_ticks < restrict_min ||
        restricted_master_ticks > restrict_max) {
        std::printf("test_partner_gdp_memacc: FAIL ultimate %u-column restricted ticks=%zu\n",
                    columns132 ? 132u : 80u, restricted_master_ticks);
        return false;
    }

    /* Reproduce avdc_write_addr_at_cursor(): one access wait legitimately
       covers two short READY handshakes because both finish in the safe span. */
    if (!avdc_wait_access_like_hardware(emu) ||
        !avdc_wait_ready_like_hardware(emu)) return false;
    emu.write_port(0x3C, 0x00);
    emu.write_port(0x3D, 0x00);
    emu.write_port(0x34, 0x34);
    emu.write_port(0x35, 0x00);
    emu.write_port(0x39, 0xAB);
    if (!avdc_wait_ready_like_hardware(emu)) return false;
    emu.write_port(0x34, 0x12);
    emu.write_port(0x35, 0x00);
    emu.write_port(0x39, 0xAB);
    if (!avdc_wait_ready_like_hardware(emu) ||
        emu.get_avdc().vram[0] != 0x34u ||
        emu.get_avdc().vram[1] != 0x12u ||
        emu.get_avdc().cursor_addr != 2u ||
        (emu.read_port(0x36) & 0x10u) != 0u) {
        std::printf("test_partner_gdp_memacc: FAIL ultimate %u-column two-byte row pointer\n",
                    columns132 ? 132u : 80u);
        return false;
    }
    return true;
}

int main()
{
    if (!test_gdp_pixel_read_latch())
        return 1;
    if (!test_user_defined_characters_and_dot_stretch())
        return 1;
    if (!test_user_defined_character_port_protocol())
        return 1;
    if (!test_ultimate_avdc_access_and_ready(false) ||
        !test_ultimate_avdc_access_and_ready(true))
        return 1;

    partner_gdp_test_shim emu(terminal_profile::vt100_ansi);
    if (emu.get_sio_device_config(partner::sio_port_id::sio1_b).kind !=
        partner::sio_device_kind::internal_squid) {
        std::puts("test_partner_gdp_memacc: FAIL internal Squid is not on default port 2");
        return 1;
    }
    partner::sio_device_config moved_squid;
    moved_squid.kind = partner::sio_device_kind::internal_squid;
    if (!emu.set_sio_device_config(partner::sio_port_id::sio2_a, moved_squid) ||
        emu.get_sio_device_config(partner::sio_port_id::sio1_b).kind !=
            partner::sio_device_kind::none ||
        emu.get_sio_device_config(partner::sio_port_id::sio2_a).kind !=
            partner::sio_device_kind::internal_squid) {
        std::puts("test_partner_gdp_memacc: FAIL internal Squid did not move to port 3");
        return 1;
    }
    emu.reset();

    emu.key_input('Q');
    for (size_t i = 0; i < 400; ++i)
        emu.tick();
    if (emu.pending_key_count() != 1u) {
        std::puts("test_partner_gdp_memacc: FAIL key lost while SIO RX disabled");
        return 1;
    }
    emu.reset();

    configure_ultimate_avdc(emu, true);
    bool saw_restrict_high = false;
    bool saw_restrict_low = false;
    for (size_t i = 0; i < 100000; i++) {
        emu.tick();
        if (emu.read_port(0x36) & 0x10u)
            saw_restrict_high = true;
        else
            saw_restrict_low = true;
    }
    if (!saw_restrict_high || !saw_restrict_low) {
        std::printf("test_partner_gdp_memacc: FAIL restriction high=%d low=%d\n",
                    saw_restrict_high ? 1 : 0, saw_restrict_low ? 1 : 0);
        return 1;
    }

    // PIO-B CA5:CA6 program the SCB2675 character divider and CA7 selects
    // the GDP board's 18/24 MHz DCLK path.  Verify these are actual AVDC
    // character clocks, not render-only mode flags.
    emu.reset();
    emu.write_port(0x33, 0x07);
    emu.write_port(0x33, 0x0F); // PIO B mode 0, all outputs
    emu.write_port(0x39, 0x10); // IR pointer = 0
    const uint8_t timing_ir[] = {
        0x00, 0x03, 0x01, 0x00, 0x00, 0x03, 0x00, 0x00
    }; // 16 CCLK per line
    for (uint8_t value : timing_ir)
        emu.write_port(0x38, value);
    emu.write_port(0x32, 0x40); // CA6: C1:C0=10 -> 8 dots, 18 MHz
    emu.write_port(0x39, 0x00); // restart AVDC raster phase
    for (int i = 0; i < 4; ++i)
        emu.tick();
    if (emu.get_avdc().raster_char != 2u) { // 18/8 MHz for 1 us = 2.25 CCLK
        std::printf("test_partner_gdp_memacc: FAIL 18MHz AVDC phase=%u\n",
                    (unsigned)emu.get_avdc().raster_char);
        return 1;
    }
    emu.write_port(0x32, 0xC0); // same width, 24 MHz path
    emu.write_port(0x39, 0x00);
    for (int i = 0; i < 4; ++i)
        emu.tick();
    if (emu.get_avdc().raster_char != 3u) { // 24/8 MHz for 1 us = 3 CCLK
        std::printf("test_partner_gdp_memacc: FAIL 24MHz AVDC phase=%u\n",
                    (unsigned)emu.get_avdc().raster_char);
        return 1;
    }

    // ST8 carries the PAL-conditioned AVDINT- signal, not raw vertical blank.
    // It drives the GDP PIO's active-low BSTB and the optional CTC3 jumper.
    emu.reset();
    emu.write_port(0xCB, Z80CTC_CTRL_EI | Z80CTC_CTRL_MODE_COUNTER |
                         Z80CTC_CTRL_EDGE_RISING |
                         Z80CTC_CTRL_CONST_FOLLOWS |
                         Z80CTC_CTRL_CONTROL);
    emu.write_port(0xCB, 1);
    emu.mutable_avdc().irq_status = 0;
    emu.tick();
    emu.mutable_avdc().irq_status = 0x10;
    emu.tick();
    if ((emu.get_gdp_pio().pins & Z80PIO_BSTB) != 0u ||
        (emu.get_gdp_pio_port_a() & 0x60u) != 0u) {
        std::puts("test_partner_gdp_memacc: FAIL AVDINT did not drive BSTB-only wiring");
        return 1;
    }
    emu.tick();
    if (emu.get_ctc().chn[3].int_state == 0) {
        std::puts("test_partner_gdp_memacc: FAIL conditioned AVDINT did not clock CTC3");
        return 1;
    }

    // EF9367 IRQ is separately wired to ASTB and is likewise absent from the
    // PIO A data bus on the original board.
    emu.write_port(0x21, 0x40); // ready interrupt enabled
    emu.write_port(0x20, 0x00);
    for (int i = 0; i < 16; ++i)
        emu.tick();
    if ((emu.get_gdp_pio().pins & Z80PIO_ASTB) != 0u ||
        (emu.get_gdp_pio_port_a() & 0x60u) != 0u) {
        std::puts("test_partner_gdp_memacc: FAIL EF IRQ did not drive ASTB-only wiring");
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
