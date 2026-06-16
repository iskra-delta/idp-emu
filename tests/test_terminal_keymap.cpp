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
        if (!map_terminal_key(SDLK_UP, out, false) ||
            !expect_bytes(out, { 0x1B, 'A' }, "up_arrow"))
        {
            fails++;
        }
    }

    {
        std::vector<uint8_t> out;
        if (!map_terminal_key(SDLK_RIGHT, out, false) ||
            !expect_bytes(out, { 0x1B, 'C' }, "right_arrow"))
        {
            fails++;
        }
    }

    {
        std::vector<uint8_t> out;
        if (!map_terminal_key(SDLK_DELETE, out, false) ||
            !expect_bytes(out, { 0xFE }, "delete_setup"))
        {
            fails++;
        }
    }

    {
        std::vector<uint8_t> out;
        if (!map_terminal_key(SDLK_PAUSE, out, false) ||
            !expect_bytes(out, { 0xFE }, "pause_setup"))
        {
            fails++;
        }
    }

    {
        std::vector<uint8_t> out;
        if (map_terminal_key(SDLK_F1, out, false)) {
            std::puts("FAIL f1_plain: unexpected DEC setup mapping in plain mode");
            fails++;
        }
    }

    {
        std::vector<uint8_t> out;
        if (!map_terminal_key(SDLK_F1, out, true) ||
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
