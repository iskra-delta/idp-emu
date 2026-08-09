#include "gui/terminal_keymap.hpp"

#include <SDL.h>

#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

bool expect_bytes(const std::vector<uint8_t>& got,
                  const std::vector<uint8_t>& expected,
                  const char* label)
{
    if (got == expected) {
        return true;
    }
    std::printf("FAIL %s: expected", label);
    for (uint8_t value : expected) {
        std::printf(" %02x", (unsigned)value);
    }
    std::printf(" got");
    for (uint8_t value : got) {
        std::printf(" %02x", (unsigned)value);
    }
    std::printf("\n");
    return false;
}

}

int main()
{
    int fails = 0;

    {
        terminal_key_repeat_limiter limiter;
        if (!limiter.accept(SDLK_LEFT, false, 1000) ||
            limiter.accept(SDLK_LEFT, true, 1040) ||
            !limiter.accept(SDLK_LEFT, true, 1100) ||
            limiter.accept(SDLK_LEFT, true, 1150) ||
            !limiter.accept(SDLK_LEFT, true, 1200))
        {
            std::puts("FAIL repeat_limiter_rate");
            fails++;
        }
        limiter.release(SDLK_LEFT);
        if (!limiter.accept(SDLK_LEFT, true, 1210)) {
            std::puts("FAIL repeat_limiter_release");
            fails++;
        }
        if (limiter.accept(SDLK_RIGHT, true, 1300, false)) {
            std::puts("FAIL repeat_limiter_disabled");
            fails++;
        }
    }

    {
        std::vector<uint8_t> out;
        if (!map_terminal_ctrl_key(SDLK_s, KMOD_CTRL, out) ||
            !expect_bytes(out, { 0x13 }, "ctrl_s"))
        {
            fails++;
        }
    }

    {
        std::vector<uint8_t> out;
        if (!map_terminal_ctrl_key(SDLK_c, KMOD_CTRL, out) ||
            !expect_bytes(out, { 0x03 }, "ctrl_c"))
        {
            fails++;
        }
    }

    {
        std::vector<uint8_t> out;
        if (!map_terminal_key(SDLK_UP, out, terminal_profile::vt52, false) ||
            !expect_bytes(out, { 0x1B, 'A' }, "vt52_up_arrow"))
        {
            fails++;
        }
    }

    {
        std::vector<uint8_t> out;
        if (!map_terminal_key(SDLK_RIGHT, out, terminal_profile::vt52, false) ||
            !expect_bytes(out, { 0x1B, 'C' }, "vt52_right_arrow"))
        {
            fails++;
        }
    }

    {
        std::vector<uint8_t> out;
        if (!map_terminal_key(SDLK_UP, out, terminal_profile::vt100_ansi, true) ||
            !expect_bytes(out, { 0x1B, '[', 'A' }, "ansi_up_arrow"))
        {
            fails++;
        }
    }

    {
        std::vector<uint8_t> out;
        if (!map_terminal_key(SDLK_LEFT, out, terminal_profile::vt100_ansi, true) ||
            !expect_bytes(out, { 0x1B, '[', 'D' }, "ansi_left_arrow"))
        {
            fails++;
        }
    }

    {
        std::vector<uint8_t> out;
        if (!map_terminal_key(SDLK_BACKSPACE, out, terminal_profile::vt52, false) ||
            !expect_bytes(out, { 0x08 }, "backspace"))
        {
            fails++;
        }
    }

    {
        std::vector<uint8_t> out;
        if (!map_terminal_key(SDLK_DELETE, out, terminal_profile::vt52, false) ||
            !expect_bytes(out, { 0x7F }, "delete"))
        {
            fails++;
        }
    }

    {
        std::vector<uint8_t> out;
        if (!map_terminal_key(SDLK_PAUSE, out, terminal_profile::vt52, false) ||
            !expect_bytes(out, { 0xFE }, "pause_setup"))
        {
            fails++;
        }
    }

    {
        std::vector<uint8_t> out;
        if (map_terminal_key(SDLK_F1, out, terminal_profile::vt52, false)) {
            std::puts("FAIL f1_plain: unexpected DEC setup mapping in plain mode");
            fails++;
        }
    }

    {
        std::vector<uint8_t> out;
        if (!map_terminal_key(SDLK_F1, out, terminal_profile::vt100_ansi, true) ||
            !expect_bytes(out, { 0x1B, 0x14 }, "f1_dec_setup"))
        {
            fails++;
        }
    }

    if (fails == 0) {
        std::puts("test_terminal_keymap: PASS");
        return 0;
    }

    std::printf("test_terminal_keymap: %d failure(s)\n", fails);
    return 1;
}
