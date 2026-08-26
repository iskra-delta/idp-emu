#include <cstdint>
#include <cstring>
#include <cstdio>
#include <vector>

#define CHIPS_IMPL
#include "ef9367.h"

namespace {

static bool pixel_on(const ef9367_t& gdp, int x, int y)
{
    if ((x < 0) || (x >= 1024) || (y < 0) || (y >= 512)) {
        return false;
    }
    const int fb_y = 511 - y;
    const uint32_t p = (uint32_t)fb_y * 1024u + (uint32_t)x;
    const uint32_t byte_ix = p >> 3;
    const uint8_t bit = (uint8_t)(1u << (p & 7u));
    return (gdp.fb[0][byte_ix] & bit) != 0;
}

static void pen_down(ef9367_t* gdp)
{
    ef9367_command(gdp, 0x00); // pen
    ef9367_command(gdp, 0x02); // down
}

static int count_lit(const ef9367_t& gdp, int x0, int y0, int x1, int y1)
{
    const int lo_x = (x0 < x1) ? x0 : x1;
    const int hi_x = (x0 > x1) ? x0 : x1;
    const int lo_y = (y0 < y1) ? y0 : y1;
    const int hi_y = (y0 > y1) ? y0 : y1;
    int n = 0;
    for (int y = lo_y; y <= hi_y; y++) {
        for (int x = lo_x; x <= hi_x; x++) {
            if (pixel_on(gdp, x, y)) {
                n++;
            }
        }
    }
    return n;
}

static uint64_t idle_pins()
{
    return EF9367_CS | EF9367_RD | EF9367_WR | EF9367_RESET |
           EF9367_FM0 | EF9367_FM1 | EF9367_SCRLM |
           EF9367_IRQ | EF9367_MW;
}

static int test_standard_vector_commands_draw_and_update_xy()
{
    int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

    ef9367_t gdp{};
    ef9367_init(&gdp);
    pen_down(&gdp);

    gdp.x = 100;
    gdp.y = 100;
    gdp.dx = 6;
    gdp.dy = 4;
    ef9367_command(&gdp, 0x11); // +X,+Y
    CHECK(gdp.x == 106);
    CHECK(gdp.y == 104);
    CHECK(pixel_on(gdp, 100, 100));
    CHECK(pixel_on(gdp, 106, 104));

    gdp.x = 200;
    gdp.y = 200;
    gdp.dx = 3;
    gdp.dy = 5;
    ef9367_command(&gdp, 0x13); // -X,+Y
    CHECK(gdp.x == 197);
    CHECK(gdp.y == 205);
    CHECK(pixel_on(gdp, 200, 200));
    CHECK(pixel_on(gdp, 197, 205));

    gdp.x = 100;
    gdp.y = 100;
    gdp.dx = 9;
    gdp.dy = 7;
    ef9367_command(&gdp, 0x10); // axis +X
    CHECK(gdp.x == 109);
    CHECK(gdp.y == 100);

    gdp.x = 100;
    gdp.y = 100;
    gdp.dx = 9;
    gdp.dy = 7;
    ef9367_command(&gdp, 0x12); // axis +Y
    CHECK(gdp.x == 100);
    CHECK(gdp.y == 107);

    gdp.x = 100;
    gdp.y = 100;
    gdp.dx = 9;
    gdp.dy = 7;
    ef9367_command(&gdp, 0x14); // axis -Y
    CHECK(gdp.x == 100);
    CHECK(gdp.y == 93);

    gdp.x = 100;
    gdp.y = 100;
    gdp.dx = 9;
    gdp.dy = 7;
    ef9367_command(&gdp, 0x16); // axis -X
    CHECK(gdp.x == 91);
    CHECK(gdp.y == 100);

#undef CHECK
    return fails;
}

static int test_small_vector_command_path_from_cmd_port()
{
    int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

    ef9367_t gdp{};
    ef9367_init(&gdp);
    pen_down(&gdp);

    gdp.x = 30;
    gdp.y = 30;
    ef9367_write(&gdp, 0x20, 0xDD); // b7=1, dx=2, dy=3, dir=0x4 (+X,-Y), non-axis

    CHECK(gdp.x == 32);
    CHECK(gdp.y == 27);
    CHECK(pixel_on(gdp, 30, 30));
    CHECK(pixel_on(gdp, 32, 27));

#undef CHECK
    return fails;
}

static int test_pen_up_moves_without_drawing()
{
    int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

    ef9367_t gdp{};
    ef9367_init(&gdp);
    // Default is pen-up.
    gdp.x = 50;
    gdp.y = 60;
    gdp.dx = 4;
    gdp.dy = 0;
    ef9367_command(&gdp, 0x11);
    CHECK(gdp.x == 54);
    CHECK(gdp.y == 60);
    CHECK(!pixel_on(gdp, 50, 60));
    CHECK(!pixel_on(gdp, 54, 60));

#undef CHECK
    return fails;
}

static int test_reverse_erase_clears_vector()
{
    int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

    ef9367_t gdp{};
    ef9367_init(&gdp);
    pen_down(&gdp);

    // Draw an oblique vector A->B.
    gdp.x = 120;
    gdp.y = 120;
    gdp.dx = 13;
    gdp.dy = 29;
    ef9367_command(&gdp, 0x11); // +X,+Y
    CHECK(count_lit(gdp, 100, 100, 200, 200) > 0);

    // Erase the same vector in reverse direction B->A.
    ef9367_command(&gdp, 0x01); // eraser selected, keep "down"
    gdp.x = 133;
    gdp.y = 149;
    gdp.dx = 13;
    gdp.dy = 29;
    ef9367_command(&gdp, 0x17); // -X,-Y

    CHECK(count_lit(gdp, 100, 100, 200, 200) == 0);

#undef CHECK
    return fails;
}

static int test_ctrl2_vector_line_styles()
{
    int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

    static constexpr bool expected[4][16] = {
        { true, true, true, true, true, true, true, true,
          true, true, true, true, true, true, true, true },
        { true, true, false, false, true, true, false, false,
          true, true, false, false, true, true, false, false },
        { true, true, true, true, false, false, false, false,
          true, true, true, true, false, false, false, false },
        { true, true, true, true, true, true, true, true,
          true, true, false, false, true, true, false, false },
    };

    for (uint8_t style = 0; style < 4; ++style) {
        ef9367_t gdp{};
        ef9367_init(&gdp);
        pen_down(&gdp);
        gdp.cr2 = style;
        gdp.x = 20;
        gdp.y = 30;
        gdp.dx = 15;
        gdp.dy = 0;
        ef9367_command(&gdp, 0x10);

        for (int i = 0; i < 16; ++i) {
            CHECK(pixel_on(gdp, 20 + i, 30) == expected[style][i]);
        }
        CHECK(gdp.x == 35);
        CHECK(gdp.y == 30);
    }

#undef CHECK
    return fails;
}

static int test_command_busy_timing()
{
    int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

    ef9367_t gdp{};
    ef9367_init(&gdp);
    pen_down(&gdp);

    gdp.dx = 0;
    gdp.dy = 0;
    ef9367_command(&gdp, 0x11);
    CHECK(gdp.busy_ticks == 14u); // ceil((2 sync + 2 init + 1 dot) CK * 8/3)
    const uint32_t dot_busy_ticks = gdp.busy_ticks;
    CHECK((ef9367_read(&gdp, 0x2F) & 0x04u) == 0u);

    uint64_t pins = idle_pins();
    for (uint32_t i = 0; i < 13u; ++i) {
        pins = ef9367_tick(&gdp, pins);
        CHECK((ef9367_read(&gdp, 0x2F) & 0x04u) == 0u);
    }
    pins = ef9367_tick(&gdp, pins);
    CHECK((ef9367_read(&gdp, 0x2F) & 0x04u) != 0u);

    ef9367_reset(&gdp);
    gdp.dx = 7;
    gdp.dy = 2;
    ef9367_command(&gdp, 0x11);
    CHECK(gdp.busy_ticks == 32u); // (4 overhead + 8 dots) CK * 8/3
    const uint32_t short_vector_busy_ticks = gdp.busy_ticks;

    ef9367_reset(&gdp);
    gdp.dx = 100;
    gdp.dy = 10;
    ef9367_command(&gdp, 0x11);
    CHECK(gdp.busy_ticks == 280u); // (4 overhead + 101 dots) CK * 8/3
    const uint32_t long_vector_busy_ticks = gdp.busy_ticks;

    ef9367_reset(&gdp);
    gdp.ch_size = 0x11;
    ef9367_write(&gdp, 0x20, 0x41);
    CHECK(gdp.busy_ticks == 128u); // 6P * 8Q = 48 CK
    const uint32_t character_busy_ticks = gdp.busy_ticks;

    ef9367_reset(&gdp);
    pins = ef9367_tick(&gdp, idle_pins());
    CHECK(gdp.ck_phase == 3u);
    gdp.ch_size = 0x11;
    ef9367_write(&gdp, 0x20, 0x41);
    CHECK(gdp.busy_ticks == 127u); // same 48 CK, starting 3/8 CK into a cycle

    ef9367_reset(&gdp);
    ef9367_command(&gdp, 0x04);
    CHECK(gdp.busy_ticks == 143616u); // to line 36, then two 25,200-CK fields

    ef9367_reset(&gdp);
    gdp.mode_512_lines = false;
    ef9367_command(&gdp, 0x04);
    CHECK(gdp.busy_ticks == 76416u); // one field in non-interlaced format

    ef9367_reset(&gdp);
    gdp.scan_ctr = 36u * 96u - 1u;
    ef9367_command(&gdp, 0x04);
    CHECK(gdp.busy_ticks == 134403u); // one CK to VB falling, then two fields

    ef9367_reset(&gdp);
    gdp.cr1 = 0x02; // pen-selected screen scan fills the selected page
    ef9367_command(&gdp, 0x0C);
    CHECK(gdp.fb[0][0] == 0xFFu);
    CHECK(gdp.fb[0][sizeof(gdp.fb[0]) - 1u] == 0xFFu);
    CHECK(gdp.busy_ticks == 143616u);
    const uint32_t clear_busy_ticks = gdp.busy_ticks;
    CHECK(dot_busy_ticks < short_vector_busy_ticks);
    CHECK(short_vector_busy_ticks < character_busy_ticks);
    CHECK(character_busy_ticks < long_vector_busy_ticks);
    CHECK(long_vector_busy_ticks < clear_busy_ticks);

#undef CHECK
    return fails;
}

static int test_xor_mode_toggles_pen_and_eraser_plots()
{
    int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

    ef9367_t gdp{};
    ef9367_init(&gdp);
    pen_down(&gdp);
    gdp.xor_mode = true;

    gdp.x = 40;
    gdp.y = 40;
    gdp.dx = 6;
    gdp.dy = 4;
    ef9367_command(&gdp, 0x11);
    CHECK(count_lit(gdp, 30, 30, 60, 60) > 0);

    // Same draw in XOR mode with pen toggles pixels back off.
    gdp.x = 40;
    gdp.y = 40;
    gdp.dx = 6;
    gdp.dy = 4;
    ef9367_command(&gdp, 0x11);
    CHECK(count_lit(gdp, 30, 30, 60, 60) == 0);

    // The board-level XOR operation also applies when the GDP's eraser is
    // selected. Seed a non-uniform background so this checks exact inversion,
    // not merely drawing onto a blank raster.
    for (size_t i = 0; i < sizeof(gdp.fb[0]); ++i)
        gdp.fb[0][i] = (uint8_t)(i * 37u + (i >> 3));
    const std::vector<uint8_t> original(gdp.fb[0],
                                        gdp.fb[0] + sizeof(gdp.fb[0]));
    ef9367_command(&gdp, 0x01); // eraser
    gdp.x = 40;
    gdp.y = 40;
    gdp.dx = 6;
    gdp.dy = 4;
    ef9367_command(&gdp, 0x11);
    CHECK(std::memcmp(gdp.fb[0], original.data(), original.size()) != 0);

    // A second identical eraser plot restores every background byte.
    gdp.x = 40;
    gdp.y = 40;
    gdp.dx = 6;
    gdp.dy = 4;
    ef9367_command(&gdp, 0x11);
    CHECK(std::memcmp(gdp.fb[0], original.data(), original.size()) == 0);

#undef CHECK
    return fails;
}

static int test_xy_persists_across_control_changes_and_vectors()
{
    int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

    ef9367_t gdp{};
    ef9367_init(&gdp);
    pen_down(&gdp);
    gdp.x = 100;
    gdp.y = 100;
    gdp.dx = 4;
    gdp.dy = 3;
    ef9367_command(&gdp, 0x11);
    CHECK(gdp.x == 104u && gdp.y == 103u);

    // Board XOR drawing mode and CTRL1 pen-selection commands do not address X/Y.
    ef9367_set_board_inputs(&gdp, idle_pins() | EF9367_XORM);
    CHECK(gdp.xor_mode);
    CHECK(gdp.x == 104u && gdp.y == 103u);
    ef9367_command(&gdp, 0x01); // eraser
    CHECK(gdp.x == 104u && gdp.y == 103u);
    ef9367_command(&gdp, 0x00); // pen
    CHECK(gdp.x == 104u && gdp.y == 103u);

    // Do not rewrite X/Y: the next vector starts at the previous endpoint.
    gdp.dx = 2;
    gdp.dy = 1;
    ef9367_command(&gdp, 0x15); // +X,-Y
    CHECK(gdp.x == 106u && gdp.y == 102u);
    CHECK(!pixel_on(gdp, 104, 103)); // XOR toggled the preserved start point
    CHECK(pixel_on(gdp, 106, 102));

#undef CHECK
    return fails;
}

static int test_zero_delta_oblique_vectors_match_axis_commands()
{
    int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

    static constexpr uint8_t directions[] = {0x00, 0x02, 0x04, 0x06};
    for (uint8_t direction : directions) {
        for (uint8_t control1 = 0; control1 < 4; ++control1) {
            for (uint8_t line_style = 0; line_style < 4; ++line_style) {
                for (int xor_mode = 0; xor_mode < 2; ++xor_mode) {
                    for (int zero_delta = 0; zero_delta < 2; ++zero_delta) {
                        ef9367_t oblique{};
                        ef9367_t axis{};
                        ef9367_init(&oblique);
                        ef9367_init(&axis);
                        for (size_t i = 0; i < sizeof(oblique.fb[0]); ++i)
                            oblique.fb[0][i] = (uint8_t)(i * 29u + (i >> 2));
                        std::memcpy(axis.fb, oblique.fb, sizeof(oblique.fb));

                        oblique.cr1 = axis.cr1 = control1;
                        oblique.cr2 = axis.cr2 = line_style;
                        oblique.xor_mode = axis.xor_mode = xor_mode != 0;
                        oblique.x = axis.x = 512u;
                        oblique.y = axis.y = 256u;
                        oblique.dx = axis.dx = zero_delta == 0 ? 0u : 11u;
                        oblique.dy = axis.dy = zero_delta == 0 ? 11u : 0u;

                        const uint8_t oblique_command =
                            (uint8_t)(0x11u | direction);
                        uint8_t axis_command = 0;
                        if (zero_delta == 0) {
                            axis_command = (direction & 0x04u) ? 0x14u : 0x12u;
                        } else {
                            axis_command = (direction & 0x02u) ? 0x16u : 0x10u;
                        }
                        ef9367_command(&oblique, oblique_command);
                        ef9367_command(&axis, axis_command);

                        CHECK(oblique.x == axis.x);
                        CHECK(oblique.y == axis.y);
                        CHECK(oblique.busy_ticks == axis.busy_ticks);
                        CHECK(std::memcmp(oblique.fb, axis.fb,
                                          sizeof(oblique.fb)) == 0);
                    }
                }
            }
        }
    }

#undef CHECK
    return fails;
}

static int test_home_and_block_commands()
{
    int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

    ef9367_t gdp{};
    ef9367_init(&gdp);
    pen_down(&gdp);

    gdp.x = 123;
    gdp.y = 77;
    ef9367_command(&gdp, 0x05);
    CHECK(gdp.x == 0);
    CHECK(gdp.y == 0);

    gdp.x = 10;
    gdp.y = 20;
    gdp.ch_size = 0x11; // P=1, Q=1
    ef9367_command(&gdp, 0x0A); // 5P x 8Q solid character block
    CHECK(gdp.x == 16); // five drawn columns plus normal one-column spacing
    CHECK(gdp.y == 20);
    CHECK(count_lit(gdp, 10, 20, 14, 27) == 40);

    ef9367_command(&gdp, 0x05);
    ef9367_command(&gdp, 0x04); // clear current page
    gdp.y = 20;
    ef9367_command(&gdp, 0x0B); // 4x4 block
    CHECK(gdp.x == 4);
    CHECK(gdp.y == 20);
    CHECK(count_lit(gdp, 0, 20, 3, 23) == 16);

#undef CHECK
    return fails;
}

static int test_extended_vectors_and_bresenham()
{
    int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

    struct endpoint { uint16_t x; uint16_t y; };
    static constexpr endpoint expected[8] = {
        { 107, 100 }, { 107, 107 }, { 100, 107 }, { 93, 107 },
        { 100, 93 },  { 107, 93 },  { 93, 100 },  { 93, 93 }
    };
    for (uint8_t i = 0; i < 8; ++i) {
        ef9367_t gdp{};
        ef9367_init(&gdp);
        pen_down(&gdp);
        gdp.x = 100;
        gdp.y = 100;
        gdp.dx = 7;
        gdp.dy = 3;
        ef9367_command(&gdp, (uint8_t)(0x18u + i));
        CHECK(gdp.x == expected[i].x);
        CHECK(gdp.y == expected[i].y);
        CHECK(pixel_on(gdp, 100, 100));
        CHECK(pixel_on(gdp, expected[i].x, expected[i].y));
    }

    ef9367_t line{};
    ef9367_init(&line);
    pen_down(&line);
    line.x = 10;
    line.y = 10;
    line.dx = 6;
    line.dy = 4;
    ef9367_command(&line, 0x11);
    static constexpr endpoint bresenham[] = {
        {10,10}, {11,11}, {12,11}, {13,12}, {14,13}, {15,13}, {16,14}
    };
    CHECK(count_lit(line, 10, 10, 16, 14) == 7);
    for (const endpoint& p : bresenham)
        CHECK(pixel_on(line, p.x, p.y));

    /* Line patterns restart at the command origin. A right-to-left line is
       therefore the spatial reverse of the same pattern drawn left-to-right,
       matching the Partner hardware observation without reversing the mask. */
    ef9367_clear_framebuffers(&line);
    line.cr2 = 1;
    line.x = 35;
    line.y = 20;
    line.dx = 15;
    line.dy = 0;
    ef9367_command(&line, 0x16);
    CHECK(pixel_on(line, 35, 20));
    CHECK(pixel_on(line, 34, 20));
    CHECK(!pixel_on(line, 33, 20));
    CHECK(!pixel_on(line, 32, 20));

#undef CHECK
    return fails;
}

static int test_character_orientations()
{
    int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

    for (uint8_t orientation = 0; orientation < 4; ++orientation) {
        ef9367_t gdp{};
        ef9367_init(&gdp);
        pen_down(&gdp);
        gdp.ch_size = 0x11;
        gdp.cr2 = (uint8_t)(orientation << 2);
        gdp.x = 20;
        gdp.y = 20;
        ef9367_command(&gdp, 0x0A);
        if (orientation == 0) {
            CHECK(pixel_on(gdp, 20, 20));
            CHECK(pixel_on(gdp, 24, 27));
            CHECK(gdp.x == 26 && gdp.y == 20);
        } else if (orientation == 1) {
            CHECK(pixel_on(gdp, 20, 20));
            CHECK(pixel_on(gdp, 31, 27));
            CHECK(gdp.x == 26 && gdp.y == 20);
        } else if (orientation == 2) {
            CHECK(pixel_on(gdp, 20, 20));
            CHECK(pixel_on(gdp, 13, 24));
            CHECK(gdp.x == 20 && gdp.y == 26);
        } else {
            CHECK(pixel_on(gdp, 20, 20));
            CHECK(pixel_on(gdp, 13, 31));
            CHECK(gdp.x == 20 && gdp.y == 26);
        }
    }

#undef CHECK
    return fails;
}

static int test_status_interrupt_lightpen_and_memory_request()
{
    int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

    ef9367_t gdp{};
    ef9367_init(&gdp);
    gdp.cr1 = 0x40;
    ef9367_command(&gdp, 0x00);
    uint64_t pins = idle_pins();
    while (gdp.busy_ticks)
        pins = ef9367_tick(&gdp, pins);
    CHECK((ef9367_read(&gdp, 0x2F) & 0xC0u) == 0xC0u);
    CHECK((pins & EF9367_IRQ) == 0u);
    CHECK((ef9367_read(&gdp, 0x20) & 0xC0u) == 0xC0u);
    CHECK((ef9367_read(&gdp, 0x2F) & 0xF0u) == 0u);

    ef9367_reset(&gdp);
    gdp.cr1 = 0x20;
    gdp.vblank = false;
    gdp.previous_vblank = false;
    gdp.scan_ctr = 244u * 96u - 1u;
    gdp.ck_phase = 7;
    pins = ef9367_tick(&gdp, idle_pins());
    CHECK((ef9367_read(&gdp, 0x2F) & 0xA0u) == 0xA0u);
    CHECK((pins & EF9367_IRQ) == 0u);

    ef9367_reset(&gdp);
    gdp.cr1 = 0x10;
    gdp.vblank = false;
    gdp.previous_vblank = false;
    gdp.scan_ctr = 40u * 96u + 10u;
    ef9367_command(&gdp, 0x09);
    CHECK((ef9367_read(&gdp, 0x2F) & 0x01u) == 0u);
    pins = ef9367_tick(&gdp, idle_pins() | EF9367_LPCK);
    CHECK((ef9367_read(&gdp, 0x2F) & 0x91u) == 0x91u);
    CHECK((ef9367_read(&gdp, 0x2C) & 0x01u) != 0u);
    CHECK((ef9367_read(&gdp, 0x2C) & 0x01u) == 0u);

    ef9367_reset(&gdp);
    ef9367_command(&gdp, 0x0F);
    bool saw_mw = false;
    bool saw_release = false;
    pins = idle_pins();
    for (int i = 0; i < 8; ++i) {
        pins = ef9367_tick(&gdp, pins);
        if ((pins & EF9367_MW) == 0u) saw_mw = true;
        if (saw_mw && (pins & EF9367_MW) != 0u) saw_release = true;
    }
    CHECK(saw_mw && saw_release);

    gdp.cr1 = 0x04;
    pins = ef9367_tick(&gdp, idle_pins());
    CHECK((pins & EF9367_BLANK) != 0u);

    ef9367_reset(&gdp);
    pen_down(&gdp);
    gdp.x = 1024;
    gdp.y = 10;
    CHECK((ef9367_read(&gdp, 0x2F) & 0x08u) != 0u);
    gdp.cr1 |= 0x08;
    gdp.dx = 0;
    gdp.dy = 0;
    ef9367_command(&gdp, 0x11);
    CHECK(pixel_on(gdp, 0, 10));

#undef CHECK
    return fails;
}

static int test_partner_control_and_vblank_pins()
{
    int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)
    ef9367_t gdp{};
    ef9367_init(&gdp);
    uint64_t pins = EF9367_CS | EF9367_RD | EF9367_WR | EF9367_RESET |
                    EF9367_RBNK | EF9367_WBNK | EF9367_XORM |
                    EF9367_FM0 | EF9367_FM1;
    pins = ef9367_tick(&gdp, pins);
    CHECK(gdp.read_bank == 1);
    CHECK(gdp.write_bank == 1);
    CHECK(gdp.xor_mode);
    CHECK(gdp.mode_512_lines);

    const uint16_t scan_before_inputs = gdp.scan_ctr;
    const uint8_t phase_before_inputs = gdp.ck_phase;
    ef9367_set_board_inputs(&gdp, EF9367_RESET | EF9367_RBNK);
    CHECK(gdp.read_bank == 1);
    CHECK(gdp.write_bank == 0);
    CHECK(gdp.scan_ctr == scan_before_inputs);
    CHECK(gdp.ck_phase == phase_before_inputs);

    pins |= EF9367_SCROLL_LOAD;
    pins &= ~EF9367_SCRLM;
    EF9367_SET_DATA(pins, 0xF8);
    pins = ef9367_tick(&gdp, pins);
    CHECK(gdp.scroll_latch == 0xF8);
    CHECK(gdp.scroll_offset == -8);
    pins = (pins & ~EF9367_SCROLL_LOAD) | EF9367_SCRLM;
    pins = ef9367_tick(&gdp, pins);
    CHECK(gdp.scroll_offset == 0);

    bool saw_vblank = false;
    bool saw_visible = false;
    pins &= ~EF9367_SCRLM;
    for (int i = 0; i < 10000; ++i) {
        pins = ef9367_tick(&gdp, pins);
        saw_vblank = saw_vblank || ((pins & EF9367_VBLANK) != 0);
        saw_visible = saw_visible || ((pins & EF9367_VBLANK) == 0);
    }
    CHECK(saw_vblank);
    CHECK(saw_visible);
#undef CHECK
    return fails;
}

} // namespace

int main()
{
    int fails = 0;
    fails += test_standard_vector_commands_draw_and_update_xy();
    fails += test_small_vector_command_path_from_cmd_port();
    fails += test_pen_up_moves_without_drawing();
    fails += test_reverse_erase_clears_vector();
    fails += test_ctrl2_vector_line_styles();
    fails += test_command_busy_timing();
    fails += test_xor_mode_toggles_pen_and_eraser_plots();
    fails += test_xy_persists_across_control_changes_and_vectors();
    fails += test_zero_delta_oblique_vectors_match_axis_commands();
    fails += test_home_and_block_commands();
    fails += test_extended_vectors_and_bresenham();
    fails += test_character_orientations();
    fails += test_status_interrupt_lightpen_and_memory_request();
    fails += test_partner_control_and_vblank_pins();

    if (fails == 0) {
        std::printf("test_ef9367: all tests passed\n");
        return 0;
    }

    std::printf("test_ef9367: %d failure(s)\n", fails);
    return 1;
}
