#include "terminal/vt52_terminal.hpp"
#include "gui/display.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <numeric>

namespace {

constexpr int CELL_W = display::CHAR_W;
constexpr int CELL_H = display::CHAR_H;
constexpr int TEXT_W = vt52_terminal::cols * CELL_W;
constexpr int TEXT_H = vt52_terminal::rows * CELL_H;
constexpr int CURVED_MARGIN_X = 24;
constexpr int CURVED_MARGIN_Y = 12;

int cell_max(const display &disp, int col, int row)
{
    int result = 0;
    const uint8_t *fb = disp.data();
    const int x0 = CURVED_MARGIN_X + col * CELL_W;
    const int y0 = CURVED_MARGIN_Y + row * CELL_H;
    for (int y = y0; y < y0 + CELL_H; ++y) {
        for (int x = x0; x < x0 + CELL_W; ++x) {
            result = std::max<int>(result, fb[x + y * display::FB_W]);
        }
    }
    return result;
}

int cell_sum(const display &disp, int col, int row)
{
    int result = 0;
    const uint8_t *fb = disp.data();
    const int x0 = CURVED_MARGIN_X + col * CELL_W;
    const int y0 = CURVED_MARGIN_Y + row * CELL_H;
    for (int y = y0; y < y0 + CELL_H; ++y) {
        for (int x = x0; x < x0 + CELL_W; ++x) {
            result += fb[x + y * display::FB_W];
        }
    }
    return result;
}

bool expect(bool cond, const char *label)
{
    if (!cond) {
        std::printf("FAIL %s\n", label);
        return false;
    }
    return true;
}

}

int main()
{
    int fails = 0;

    display disp;
    disp.load_font("");
    disp.set_phosphor_type(display::phosphor_type::green);

    vt52_terminal term(false);
    term.put_char(0x1B); term.put_char('Y'); term.put_char(' '); term.put_char(' ');
    term.put_char(0x1B); term.put_char('b'); term.put_char('1');
    term.put_char('H');
    term.put_char(0x1B); term.put_char('Y'); term.put_char(' '); term.put_char('!');
    term.put_char(0x1B); term.put_char('p');
    term.put_char('I');
    term.put_char(0x1B); term.put_char('Y'); term.put_char(' '); term.put_char('"');
    term.put_char(0x1B); term.put_char('q');
    term.put_char('N');
    term.put_char(0x1B); term.put_char('f'); // hide cursor for deterministic framebuffer
    term.render_to(disp);

    fails += !expect(disp.content_width() == TEXT_W + CURVED_MARGIN_X * 2, "text width with margins");
    fails += !expect(disp.content_height() == TEXT_H + CURVED_MARGIN_Y * 2, "text height with margins");
    fails += !expect(!disp.preserve_aspect(), "text mode stretches to fit");

    const int hi_sum = cell_sum(disp, 0, 0);
    const int inv_sum = cell_sum(disp, 1, 0);
    const int norm_sum = cell_sum(disp, 2, 0);
    const uint8_t *fb = disp.data();
    const int hi_x0 = CURVED_MARGIN_X;
    const int hi_gap_y = CURVED_MARGIN_Y;
    fails += !expect(cell_max(disp, 0, 0) == 232, "highlight reaches bright level");
    fails += !expect(cell_max(disp, 2, 0) == 168, "normal reaches standard level");
    fails += !expect(inv_sum > hi_sum, "inverse fills background");
    fails += !expect(hi_sum > norm_sum, "highlight brighter than normal");
    fails += !expect(fb[(hi_x0 + 9) + hi_gap_y * display::FB_W] == 232, "right glyph stroke preserved");
    fails += !expect(fb[(hi_x0 + 10) + hi_gap_y * display::FB_W] == 0, "glyph cell keeps 1-pixel gap");

    term.put_char(0x1B); term.put_char('E');
    term.render_to(disp);
    int total = 0;
    fb = disp.data();
    for (int y = 0; y < TEXT_H; ++y) {
        for (int x = 0; x < TEXT_W; ++x) {
            total += fb[x + y * display::FB_W];
        }
    }
    fails += !expect(total == 0, "clear screen empties text raster");

    vt52_terminal ansi(false);
    ansi.put_char(0x1B); ansi.put_char('['); ansi.put_char('1'); ansi.put_char('m');
    ansi.put_char('H');
    ansi.put_char(0x1B); ansi.put_char('['); ansi.put_char('7'); ansi.put_char('m');
    ansi.put_char('I');
    ansi.put_char(0x1B); ansi.put_char('['); ansi.put_char('0'); ansi.put_char('m');
    ansi.put_char('N');
    ansi.put_char(0x1B); ansi.put_char('['); ansi.put_char('?'); ansi.put_char('2'); ansi.put_char('5'); ansi.put_char('l');
    ansi.render_to(disp);

    fails += !expect(cell_max(disp, 0, 0) == 232, "ansi highlight reaches bright level");
    fails += !expect(cell_sum(disp, 1, 0) > cell_sum(disp, 0, 0), "ansi inverse fills background");

    total = 0;
    fb = disp.data();
    for (int y = 0; y < TEXT_H + CURVED_MARGIN_Y * 2; ++y) {
        for (int x = 0; x < TEXT_W + CURVED_MARGIN_X * 2; ++x) {
            total += fb[x + y * display::FB_W];
        }
    }
    fails += !expect(total > 0, "ansi hidden cursor keeps text only");

    ansi.put_char(0x1B); ansi.put_char('['); ansi.put_char('2'); ansi.put_char('J');
    ansi.render_to(disp);
    total = 0;
    fb = disp.data();
    for (int y = 0; y < TEXT_H + CURVED_MARGIN_Y * 2; ++y) {
        for (int x = 0; x < TEXT_W + CURVED_MARGIN_X * 2; ++x) {
            total += fb[x + y * display::FB_W];
        }
    }
    fails += !expect(total == 0, "ansi clear screen empties text raster");

    if (fails == 0) {
        std::puts("test_vt52_terminal: PASS");
        return 0;
    }

    std::printf("test_vt52_terminal: %d failure(s)\n", fails);
    return 1;
}
