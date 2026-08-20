#include <cstdint>
#include <cstdio>

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

static int test_xor_mode_toggles_with_pen_only()
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

    // In XOR mode with eraser selected, writes are no-op on pixels.
    ef9367_command(&gdp, 0x01); // eraser
    gdp.x = 40;
    gdp.y = 40;
    gdp.dx = 6;
    gdp.dy = 4;
    ef9367_command(&gdp, 0x11);
    CHECK(count_lit(gdp, 30, 30, 60, 60) == 0);

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
    ef9367_command(&gdp, 0x05); // Partner ROM-observed X home
    CHECK(gdp.x == 0);
    CHECK(gdp.y == 77);

    gdp.x = 10;
    gdp.y = 20;
    gdp.ch_size = 0x11; // P=1, Q=1
    ef9367_command(&gdp, 0x0A); // 8x8 block
    CHECK(gdp.x == 18);
    CHECK(gdp.y == 20);
    CHECK(count_lit(gdp, 10, 20, 17, 27) == 64);

    ef9367_command(&gdp, 0x05);
    ef9367_command(&gdp, 0x04); // clear current page
    ef9367_command(&gdp, 0x0B); // 4x4 block
    CHECK(gdp.x == 4);
    CHECK(gdp.y == 20);
    CHECK(count_lit(gdp, 0, 20, 3, 23) == 16);

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
    for (int i = 0; i < 1100; ++i) {
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
    fails += test_xor_mode_toggles_with_pen_only();
    fails += test_home_and_block_commands();
    fails += test_partner_control_and_vblank_pins();

    if (fails == 0) {
        std::printf("test_ef9367: all tests passed\n");
        return 0;
    }

    std::printf("test_ef9367: %d failure(s)\n", fails);
    return 1;
}
