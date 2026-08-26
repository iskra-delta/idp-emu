#include <cstdio>

#define CHIPS_IMPL
#include "scn2674.h"

namespace {

int fails = 0;
#define CHECK(c) do { if (!(c)) { \
    std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; \
} } while (0)

static uint64_t idle_pins()
{
    return SCN2674_CS | SCN2674_RD | SCN2674_WR | SCN2674_RESET;
}

static void clocks(scn2674_t& avdc, int count)
{
    uint64_t pins = idle_pins();
    while (count-- > 0)
        pins = scn2674_tick(&avdc, pins);
}

static void load_ir(scn2674_t& avdc, const uint8_t* values, int count)
{
    scn2674_write(&avdc, 0x35, 0x10);
    for (int i = 0; i < count; ++i)
        scn2674_write(&avdc, 0x34, values[i]);
}

static void load_test_timing(scn2674_t& avdc)
{
    /* 16 CCLK/line: active 4, HFP 7, HSYNC 2, HBP 3.
       12 lines/field: active 1, VFP 4, VSYNC 3, VBP 4. */
    const uint8_t ir[] = { 0x00, 0x03, 0x01, 0x00, 0x00, 0x03, 0x00, 0x00 };
    load_ir(avdc, ir, (int)sizeof(ir));
}

static void test_all_initialization_registers()
{
    scn2674_t avdc{};
    scn2674_init(&avdc);
    const uint8_t ir[15] = {
        0xD4, 0x2F, 0x8D, 0xA5, 0x99, 0xFF, 0x2A, 0xEA,
        0x34, 0xA5, 0x78, 0xC1, 0x83, 0x84, 0xB5
    };
    load_ir(avdc, ir, 15);

    CHECK(avdc.ir_ptr == 14u);
    CHECK(avdc.double_height_width_enabled);
    CHECK(avdc.composite_sync_enabled);
    CHECK(avdc.buffer_mode == 0u);
    CHECK(!avdc.interlace_enabled);
    CHECK(avdc.scanlines_per_char_row == 11u);
    CHECK(avdc.equalizing_constant == 48u);
    CHECK(avdc.hsync_width == 4u);
    CHECK(avdc.hback_porch == 19u);
    CHECK(avdc.vfront_porch == 24u);
    CHECK(avdc.vback_porch == 14u);
    CHECK(avdc.character_blink_slow);
    CHECK(avdc.rows_per_screen == 26u);
    CHECK(avdc.chars_per_row == 256u);
    CHECK(avdc.cursor_first_line == 2u);
    CHECK(avdc.cursor_last_line == 10u);
    CHECK(avdc.vsync_width == 7u);
    CHECK(avdc.cursor_blink_enabled);
    CHECK(!avdc.cursor_blink_slow);
    CHECK(avdc.underline_line == 10u);
    CHECK(avdc.display_buffer_first_addr == 0x0534u);
    CHECK(avdc.display_buffer_last_addr == 0x2BFFu);
    CHECK(avdc.display_ptr_addr == 0x0178u);
    CHECK(avdc.reset_scanline_counter_on_scrollup);
    CHECK(avdc.reset_scanline_counter_on_scrolldown);
    CHECK(avdc.scroll_start && avdc.scroll_end);
    CHECK(avdc.split_register[0] == 3u);
    CHECK(avdc.split_register[1] == 4u);
    CHECK(avdc.double_mode[0] == 2u);
    CHECK(avdc.double_mode[1] == 3u);
    CHECK(avdc.scroll_lines == 5u);
    CHECK(avdc.row_table_pending && avdc.row_table_pending_value);

    scn2674_write(&avdc, 0x34, 0x07); /* pointer remains on IR14 */
    CHECK(avdc.ir_ptr == 14u);
    CHECK(avdc.ir[14] == 0x07u);
}

static void test_screen_start_cursor_and_pointer_registers()
{
    scn2674_t avdc{};
    scn2674_init(&avdc);
    scn2674_write(&avdc, 0x35, 0x10);
    scn2674_write(&avdc, 0x34, 0x80); /* IR0 double control enabled */

    scn2674_write(&avdc, 0x36, 0x34);
    scn2674_write(&avdc, 0x37, 0xC5);
    CHECK(avdc.start1_addr == 0x0534u);
    CHECK(avdc.start1_upper_raw == 0xC5u);
    CHECK((avdc.ir[14] & 0xC0u) == 0xC0u);
    CHECK(scn2674_read(&avdc, 0x36) == 0x34u);
    CHECK(scn2674_read(&avdc, 0x37) == 0x05u);

    scn2674_write(&avdc, 0x38, 0x78);
    scn2674_write(&avdc, 0x39, 0xFE);
    CHECK(avdc.cursor_addr == 0x3E78u);
    CHECK(scn2674_read(&avdc, 0x39) == 0x3Eu);

    scn2674_write(&avdc, 0x3A, 0x9A);
    scn2674_write(&avdc, 0x3B, 0xC2);
    CHECK(avdc.start2_addr == 0x029Au);
    CHECK(avdc.start2_addr_start == 0x029Au);
    CHECK(avdc.split_use_screen2[0] && avdc.split_use_screen2[1]);
    CHECK(scn2674_read(&avdc, 0x3B) == 0x02u);
}

static void test_buffer_wraparound()
{
    scn2674_t avdc{};
    scn2674_init(&avdc);
    const uint8_t ir[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0x00, 0x13 };
    load_ir(avdc, ir, (int)sizeof(ir)); /* first=0x300, last=0x7ff */
    avdc.start1_addr = 0x07FEu;
    CHECK(scn2674_display_address(&avdc, 0) == 0x07FEu);
    CHECK(scn2674_display_address(&avdc, 1) == 0x07FFu);
    CHECK(scn2674_display_address(&avdc, 2) == 0x0300u);
    CHECK(scn2674_display_address(&avdc, 0x502u) == 0x0300u);
}

static void test_screen_start_sequencer_and_automatic_splits()
{
    scn2674_t avdc{};
    scn2674_init(&avdc);
    /* Two scan lines/row, three rows, four characters, 16 CCLK/line. */
    const uint8_t ir[] = {
        0x08, 0x03, 0x01, 0x00, 0x02, 0x03, 0x00, 0x00,
        0x00, 0x03, 0x00, 0x00, 0x01, 0x00, 0x00
    };
    load_ir(avdc, ir, (int)sizeof(ir));
    scn2674_write(&avdc, 0x36, 0x00);
    scn2674_write(&avdc, 0x37, 0x01); /* SSR1 = 0x0100 */
    avdc.display_enabled = true;

    clocks(avdc, 16); /* second scan line of row zero */
    CHECK(avdc.raster_line == 1u && avdc.row_start_addr == 0x0100u);
    scn2674_write(&avdc, 0x36, 0x00);
    scn2674_write(&avdc, 0x37, 0x02); /* takes effect on next row */
    CHECK(avdc.start1_reload_pending && avdc.row_start_addr == 0x0100u);
    clocks(avdc, 16);
    CHECK(avdc.raster_line == 2u && avdc.row_start_addr == 0x0200u);
    CHECK(!avdc.start1_reload_pending);

    scn2674_write(&avdc, 0x3A, 0x00);
    scn2674_write(&avdc, 0x3B, 0x43); /* SSR2=0x0300, SPL1 enabled */
    avdc.raster_started = false;
    avdc.raster_line = avdc.raster_char = 0u;
    avdc.start1_reload_pending = false;
    clocks(avdc, 16 * 2);
    CHECK(avdc.raster_line == 2u && avdc.row_start_addr == 0x0300u);

    /* The live MAC wraps at the IR9 end address just like display fetches. */
    avdc.raster_started = false;
    avdc.raster_line = avdc.raster_char = 0u;
    avdc.start1_addr = avdc.row_start_addr = avdc.memory_addr = 0x03FEu;
    avdc.next_row_addr = 0x03FEu;
    avdc.split_use_screen2[0] = false;
    clocks(avdc, 2);
    CHECK(avdc.memory_addr == 0x0300u);
}

static void test_soft_scroll_line_addresses()
{
    scn2674_t avdc{};
    scn2674_init(&avdc);
    const uint8_t ir[] = {
        0x18, 0x03, 0x01, 0x00, 0x03, 0x03, 0x00, 0x00,
        0x00, 0x0F, 0x00, 0x00, 0x81, 0x82, 0x02
    }; /* four lines/row; scroll rows 1..2 by two lines */
    load_ir(avdc, ir, (int)sizeof(ir));

    avdc.raster_line = 4u; /* top partial row begins at scan line 2 */
    (void)scn2674_sample_pins(&avdc, idle_pins());
    CHECK(avdc.current_line_address == 2u);
    avdc.raster_line = 11u; /* second line of the bottom partial row */
    (void)scn2674_sample_pins(&avdc, idle_pins());
    CHECK(avdc.current_line_address == 1u);

    avdc.ir[11] = 0xC0u; /* force both partial-row line addresses to zero */
    scn2674_write(&avdc, 0x35, 0x1B);
    scn2674_write(&avdc, 0x34, 0xC0);
    avdc.raster_line = 4u;
    (void)scn2674_sample_pins(&avdc, idle_pins());
    CHECK(avdc.current_line_address == 0u);
    avdc.raster_line = 11u;
    (void)scn2674_sample_pins(&avdc, idle_pins());
    CHECK(avdc.current_line_address == 0u);
}

static void test_command_groups_and_deferred_enables()
{
    scn2674_t avdc{};
    scn2674_init(&avdc);
    load_test_timing(avdc);

    avdc.status_latch = 0x1Fu;
    avdc.irq_status = 0x1Fu;
    scn2674_write(&avdc, 0x35, 0x50);
    CHECK(avdc.status_latch == 0x0Fu && avdc.irq_status == 0x0Fu);
    avdc.irq_mask = 0x1Fu;
    scn2674_write(&avdc, 0x35, 0x90);
    CHECK(avdc.irq_mask == 0x0Fu);
    avdc.irq_mask = 0u;
    scn2674_write(&avdc, 0x35, 0x70);
    CHECK(avdc.irq_mask == 0x10u);

    scn2674_write(&avdc, 0x35, 0x3D); /* display next field + cursor */
    CHECK(!avdc.display_enabled && avdc.display_enable_pending == 2u);
    CHECK(avdc.cursor_enabled);
    clocks(avdc, 16 * 12);
    CHECK(avdc.display_enabled);

    scn2674_write(&avdc, 0x35, 0x38); /* display/cursor off */
    CHECK(!avdc.display_enabled && !avdc.cursor_enabled);
    CHECK(!avdc.display_address_floating);
    scn2674_write(&avdc, 0x35, 0x2C); /* display off, float DADD */
    CHECK(avdc.display_address_floating);
    scn2674_write(&avdc, 0x35, 0x29); /* display on next scanline */
    CHECK(avdc.display_enable_pending == 1u);
    clocks(avdc, 16);
    CHECK(avdc.display_enabled);

    scn2674_write(&avdc, 0x35, 0x23); /* graphics on next row */
    CHECK(!avdc.gfx_enabled && avdc.gfx_pending);
    clocks(avdc, 16 * 11); /* currently in VBLANK: wait for next active row */
    CHECK(avdc.gfx_enabled);
    scn2674_write(&avdc, 0x35, 0x22);
    clocks(avdc, 16 * 12);
    CHECK(!avdc.gfx_enabled);
}

static void test_delayed_commands_and_ready_timing()
{
    scn2674_t avdc{};
    scn2674_init(&avdc);
    load_test_timing(avdc);
    avdc.cursor_addr = 0x0100u;
    avdc.char_latch = 0x41u;
    avdc.attr_latch = 0x82u;

    scn2674_write(&avdc, 0x35, 0xAA);
    CHECK(avdc.busy_ticks == 5u);
    CHECK((scn2674_read(&avdc, 0x35) & 0x20u) == 0u);
    clocks(avdc, 4);
    CHECK(avdc.vram[0x100] == 0x20u && avdc.busy_ticks == 1u);
    clocks(avdc, 1);
    CHECK(avdc.vram[0x100] == 0x41u && avdc.attr_vram[0x100] == 0x82u);
    CHECK(avdc.cursor_addr == 0x0100u);
    CHECK((scn2674_read(&avdc, 0x35) & 0x22u) == 0x22u);

    avdc.char_latch = 0x42u;
    scn2674_write(&avdc, 0x35, 0xAB);
    clocks(avdc, 5);
    CHECK(avdc.vram[0x100] == 0x42u && avdc.cursor_addr == 0x0101u);

    scn2674_write(&avdc, 0x35, 0xA9);
    clocks(avdc, 2);
    CHECK(avdc.cursor_addr == 0x0101u);
    clocks(avdc, 1);
    CHECK(avdc.cursor_addr == 0x0102u);

    avdc.display_ptr_addr = 0x0180u;
    avdc.char_latch = 0x31u;
    avdc.attr_latch = 0x41u;
    scn2674_write(&avdc, 0x35, 0xA2);
    clocks(avdc, 5);
    CHECK(avdc.vram[0x180] == 0x31u && avdc.attr_vram[0x180] == 0x41u);
    avdc.vram[0x180] = 0x32u;
    avdc.attr_vram[0x180] = 0x42u;
    scn2674_write(&avdc, 0x35, 0xA4);
    clocks(avdc, 5);
    CHECK(avdc.char_latch == 0x32u && avdc.attr_latch == 0x42u);

    avdc.cursor_addr = 0x0190u;
    avdc.vram[0x190] = 0x33u;
    avdc.attr_vram[0x190] = 0x43u;
    scn2674_write(&avdc, 0x35, 0xAC);
    clocks(avdc, 5);
    CHECK(avdc.char_latch == 0x33u && avdc.cursor_addr == 0x0190u);
    scn2674_write(&avdc, 0x35, 0xAD);
    clocks(avdc, 5);
    CHECK(avdc.char_latch == 0x33u && avdc.cursor_addr == 0x0191u);

    avdc.cursor_addr = 0x0200u;
    avdc.display_ptr_addr = 0x0202u;
    avdc.char_latch = 0x55u;
    avdc.attr_latch = 0xA5u;
    scn2674_write(&avdc, 0x35, 0xBB);
    CHECK(avdc.busy_ticks == 6u);
    clocks(avdc, 1);
    CHECK(avdc.vram[0x200] == 0x20u);
    clocks(avdc, 1);
    CHECK(avdc.vram[0x200] == 0x55u && avdc.cursor_addr == 0x0201u);
    clocks(avdc, 4);
    CHECK(avdc.vram[0x202] == 0x55u && avdc.cursor_addr == 0x0202u);
    CHECK(avdc.delayed_cmd == 0u);

    avdc.vram[0x300] = 0x61u;
    avdc.vram[0x301] = 0x62u;
    avdc.attr_vram[0x301] = 0x77u;
    avdc.cursor_addr = 0x0300u;
    avdc.display_ptr_addr = 0x0301u;
    scn2674_write(&avdc, 0x35, 0xBD);
    clocks(avdc, 4);
    CHECK(avdc.char_latch == 0x62u && avdc.attr_latch == 0x77u);
    CHECK(avdc.cursor_addr == 0x0301u);
}

static void test_active_display_command_waits_for_hblank()
{
    scn2674_t avdc{};
    scn2674_init(&avdc);
    load_test_timing(avdc);
    avdc.display_enabled = true;
    avdc.raster_started = true;
    avdc.raster_char = 0u;
    avdc.cursor_addr = 0x40u;
    avdc.char_latch = 0x66u;
    scn2674_write(&avdc, 0x35, 0xAA);
    clocks(avdc, 4);
    CHECK(avdc.busy_ticks == 5u && avdc.vram[0x40] == 0x20u);
    clocks(avdc, 4);
    CHECK(avdc.busy_ticks == 1u && avdc.vram[0x40] == 0x20u);
    clocks(avdc, 1);
    CHECK(avdc.vram[0x40] == 0x66u && avdc.delayed_cmd == 0u);
}

static void test_row_table_fetch_and_bus_slot_timing()
{
    scn2674_t avdc{};
    scn2674_init(&avdc);
    const uint8_t ir[] = {
        0x08, 0x03, 0x81, 0x00, 0x01, 0x03, 0x00, 0x00
    }; /* two lines/row, row table enabled, 16 CCLK/line */
    load_ir(avdc, ir, (int)sizeof(ir));
    scn2674_write(&avdc, 0x3A, 0x00);
    scn2674_write(&avdc, 0x3B, 0x01); /* table at 0x0100 */
    avdc.vram[0x100] = 0x34u;
    avdc.vram[0x101] = 0xC2u;
    avdc.display_enabled = true;
    clocks(avdc, 1);
    CHECK(avdc.use_row_table);
    CHECK(avdc.start1_addr == 0x0234u);
    CHECK(avdc.start2_addr == 0x0102u);
    CHECK((avdc.ir[14] & 0xC0u) == 0u); /* IRO[7] is not enabled */

    avdc.raster_started = true;
    avdc.raster_line = 1u; /* last scan line before the next character row */
    avdc.raster_char = avdc.chars_per_row;
    avdc.cursor_addr = 0x0200u;
    avdc.char_latch = 0x6Au;
    scn2674_write(&avdc, 0x35, 0xAA);
    clocks(avdc, 2); /* SSR fetch occupies these two blank CCLKs */
    CHECK(avdc.busy_ticks == 5u && avdc.vram[0x200] == 0x20u);
    clocks(avdc, 4);
    CHECK(avdc.busy_ticks == 1u && avdc.vram[0x200] == 0x20u);
    clocks(avdc, 1);
    CHECK(avdc.delayed_cmd == 0u && avdc.vram[0x200] == 0x6Au);
}

static void test_raster_timing_status_and_pins()
{
    scn2674_t avdc{};
    scn2674_init(&avdc);
    load_test_timing(avdc);
    CHECK(avdc.hfront_porch == 7u);

    avdc.raster_started = true;
    avdc.raster_line = 0u;
    avdc.raster_char = 0u;
    uint64_t pins = scn2674_sample_pins(&avdc, idle_pins());
    CHECK((pins & SCN2674_HSYNC) == 0u);
    CHECK((pins & SCN2674_BLANK) != 0u);
    avdc.display_enabled = true;
    pins = scn2674_sample_pins(&avdc, idle_pins());
    CHECK((pins & SCN2674_BLANK) == 0u);
    avdc.raster_char = 4u;
    pins = scn2674_sample_pins(&avdc, idle_pins());
    CHECK((pins & SCN2674_BLANK) != 0u);
    avdc.raster_char = 11u;
    pins = scn2674_sample_pins(&avdc, idle_pins());
    CHECK((pins & SCN2674_HSYNC) != 0u);
    avdc.raster_char = 13u;
    pins = scn2674_sample_pins(&avdc, idle_pins());
    CHECK((pins & SCN2674_HSYNC) == 0u);

    avdc.status_latch = avdc.irq_status = 0u;
    avdc.irq_mask = 0x18u;
    avdc.raster_started = false;
    avdc.raster_char = avdc.raster_line = 0u;
    clocks(avdc, 1);
    CHECK((avdc.status_latch & 0x08u) != 0u);
    CHECK((avdc.irq_status & 0x08u) != 0u);
    clocks(avdc, 15);
    CHECK((avdc.status_latch & 0x10u) != 0u);
    CHECK((avdc.irq_status & 0x10u) != 0u);
}

static void test_last_line_multiplex_output()
{
    scn2674_t avdc{};
    scn2674_init(&avdc);
    const uint8_t ir[] = {
        0x08, 0x03, 0x01, 0x00, 0x01, 0x03, 0x00, 0x00
    }; /* two scan lines/row, four active characters, 16 CCLK/line */
    load_ir(avdc, ir, (int)sizeof(ir));
    avdc.display_enabled = true;
    avdc.raster_started = true;

    avdc.raster_line = 0u;
    avdc.raster_char = 4u; /* blank before row 0's last scan line */
    uint64_t pins = scn2674_sample_pins(&avdc, idle_pins());
    CHECK((pins & SCN2674_LAST_LINE) != 0u);

    avdc.raster_char = 3u; /* multiplexed control is only valid in blank */
    pins = scn2674_sample_pins(&avdc, idle_pins());
    CHECK((pins & SCN2674_LAST_LINE) == 0u);

    avdc.raster_line = 1u;
    avdc.raster_char = 4u; /* blank before the following row's line zero */
    pins = scn2674_sample_pins(&avdc, idle_pins());
    CHECK((pins & SCN2674_LAST_LINE) == 0u);
}

static void test_composite_sync_equalizing_and_serration_pulses()
{
    scn2674_t avdc{};
    scn2674_init(&avdc);
    const uint8_t ir[] = {
        0x04, 0x03, 0x01, 0x00, 0x00, 0x03, 0x00, 0x00
    }; /* CSYNC, 16 CCLK/line, two-CCLK HSYNC */
    load_ir(avdc, ir, (int)sizeof(ir));
    const uint16_t vsline = (uint16_t)(1u + avdc.vfront_porch);

    avdc.raster_line = (uint16_t)(vsline - 1u); /* pre-equalizing interval */
    avdc.raster_char = 3u;
    uint64_t pins = scn2674_sample_pins(&avdc, idle_pins());
    CHECK((pins & SCN2674_VSYNC) != 0u);
    avdc.raster_char = 4u;
    pins = scn2674_sample_pins(&avdc, idle_pins());
    CHECK((pins & SCN2674_VSYNC) == 0u);

    avdc.raster_line = vsline; /* broad sync with half-line serrations */
    avdc.raster_char = 3u;
    pins = scn2674_sample_pins(&avdc, idle_pins());
    CHECK((pins & SCN2674_VSYNC) == 0u);
    avdc.raster_char = 4u;
    pins = scn2674_sample_pins(&avdc, idle_pins());
    CHECK((pins & SCN2674_VSYNC) != 0u);
}

static void test_cmac_clock_divider_ratios()
{
    scn2674_t avdc{};
    scn2674_init(&avdc);
    load_test_timing(avdc);
    avdc.raster_started = true;

    scn2674_set_clock(&avdc, 4000000u, 24000000u, 8u);
    clocks(avdc, 4);
    CHECK(avdc.raster_char == 3u);

    avdc.raster_char = 0u;
    avdc.dot_clock_accum = 0u;
    avdc.cclk_dot_phase = 0u;
    scn2674_set_clock(&avdc, 4000000u, 18000000u, 9u);
    clocks(avdc, 4);
    CHECK(avdc.raster_char == 2u);

    avdc.raster_char = 0u;
    avdc.dot_clock_accum = 0u;
    avdc.cclk_dot_phase = 0u;
    scn2674_set_clock(&avdc, 4000000u, 18000000u, 10u);
    clocks(avdc, 20);
    CHECK(avdc.raster_char == 9u);
}

static void test_interlaced_fields_and_line_addressing()
{
    scn2674_t avdc{};
    scn2674_init(&avdc);
    const uint8_t ir[] = {
        0x20, /* encoded 5 lines -> 10 lines/row over two fields */
        0x83, 0x01, 0x00, 0x00, 0x03, 0x09, 0x00
    };
    load_ir(avdc, ir, (int)sizeof(ir));
    CHECK(avdc.interlace_enabled);
    CHECK(avdc.scanlines_per_char_row == 10u);
    CHECK(avdc.scanlines_per_field_row == 5u);

    clocks(avdc, 16 * 16);
    CHECK(avdc.odd_field && avdc.field_count == 1u);
    uint64_t pins = scn2674_sample_pins(&avdc, idle_pins());
    CHECK((pins & SCN2674_ODD) != 0u);
    clocks(avdc, 17 * 16);
    CHECK(!avdc.odd_field && avdc.field_count == 2u);

    avdc.odd_field = true;
    avdc.raster_line = 2u;
    avdc.raster_char = 0u;
    scn2674_set_interlace_video(&avdc, false);
    (void)scn2674_sample_pins(&avdc, idle_pins());
    CHECK(avdc.current_line_address == 2u);
    scn2674_set_interlace_video(&avdc, true);
    (void)scn2674_sample_pins(&avdc, idle_pins());
    CHECK(avdc.current_line_address == 5u);
}

static void test_memory_interface_pins()
{
    scn2674_t avdc{};
    scn2674_init(&avdc);
    uint64_t pins = idle_pins() | SCN2674_CHAR_LOAD;
    SCN2674_SET_DATA(pins, 0x41);
    pins = scn2674_tick(&avdc, pins);
    CHECK(avdc.char_latch == 0x41u);
    pins = (pins & ~SCN2674_CHAR_LOAD) | SCN2674_ATTR_LOAD;
    SCN2674_SET_DATA(pins, 0x87);
    pins = scn2674_tick(&avdc, pins);
    CHECK(avdc.attr_latch == 0x87u);
    pins = (pins & ~SCN2674_ATTR_LOAD) | SCN2674_CHAR_OE;
    pins = scn2674_tick(&avdc, pins);
    CHECK(SCN2674_GET_DATA(pins) == 0x41u);
    pins = (pins & ~SCN2674_CHAR_OE) | SCN2674_ATTR_OE;
    pins = scn2674_tick(&avdc, pins);
    CHECK(SCN2674_GET_DATA(pins) == 0x87u);
}

static void test_external_character_rom_survives_reset()
{
    scn2674_t avdc{};
    static uint8_t rom[2048];
    rom[0] = 0xA5u;
    scn2674_init(&avdc);
    CHECK(scn2674_load_charset_rom(&avdc, rom, sizeof(rom)));
    CHECK(avdc.glyph_rom_loaded && avdc.glyph_rom[0][0] == 0xA5u);
    scn2674_reset(&avdc);
    CHECK(avdc.glyph_rom_loaded && avdc.glyph_rom[0][0] == 0xA5u);
}

}

int main()
{
    test_all_initialization_registers();
    test_screen_start_cursor_and_pointer_registers();
    test_buffer_wraparound();
    test_screen_start_sequencer_and_automatic_splits();
    test_soft_scroll_line_addresses();
    test_command_groups_and_deferred_enables();
    test_delayed_commands_and_ready_timing();
    test_active_display_command_waits_for_hblank();
    test_row_table_fetch_and_bus_slot_timing();
    test_raster_timing_status_and_pins();
    test_last_line_multiplex_output();
    test_composite_sync_equalizing_and_serration_pulses();
    test_cmac_clock_divider_ratios();
    test_interlaced_fields_and_line_addressing();
    test_memory_interface_pins();
    test_external_character_rom_survives_reset();
    if (fails == 0) std::puts("OK");
    return fails ? 1 : 0;
}
