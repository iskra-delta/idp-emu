#include "terminal_keymap.hpp"

namespace {

static inline void push_byte(std::vector<uint8_t>& out, uint8_t value)
{
    out.push_back(value);
}

}

bool map_terminal_ctrl_key(SDL_Keycode key, SDL_Keymod mods, std::vector<uint8_t>& out)
{
    if ((mods & KMOD_CTRL) == 0) {
        return false;
    }
    if (key >= SDLK_a && key <= SDLK_z) {
        push_byte(out, (uint8_t)(1 + (key - SDLK_a)));
        return true;
    }
    switch (key) {
    case SDLK_SPACE:
    case SDLK_2:
        push_byte(out, 0x00);
        return true;
    case SDLK_LEFTBRACKET:
        push_byte(out, 0x1B);
        return true;
    case SDLK_BACKSLASH:
        push_byte(out, 0x1C);
        return true;
    case SDLK_RIGHTBRACKET:
        push_byte(out, 0x1D);
        return true;
    case SDLK_6:
        push_byte(out, 0x1E);
        return true;
    case SDLK_MINUS:
        push_byte(out, 0x1F);
        return true;
    default:
        return false;
    }
}

bool map_terminal_key(SDL_Keycode key, std::vector<uint8_t>& out, bool allow_dec_setup_keys)
{
    switch (key) {
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        push_byte(out, 0x0D);
        return true;
    case SDLK_BACKSPACE:
        push_byte(out, 0x08);
        return true;
    case SDLK_TAB:
        push_byte(out, 0x09);
        return true;
    case SDLK_ESCAPE:
        push_byte(out, 0x1B);
        return true;
    case SDLK_UP:
        push_byte(out, 0x1B);
        push_byte(out, 'A');
        return true;
    case SDLK_DOWN:
        push_byte(out, 0x1B);
        push_byte(out, 'B');
        return true;
    case SDLK_RIGHT:
        push_byte(out, 0x1B);
        push_byte(out, 'C');
        return true;
    case SDLK_LEFT:
        push_byte(out, 0x1B);
        push_byte(out, 'D');
        return true;
    case SDLK_DELETE:
    case SDLK_PAUSE:
    case SDLK_F12:
        // PartOS BIOS waits for raw 0xFE during the early setup window on
        // both CRT and GDP models. Keep a consistent physical host shortcut
        // even when the active terminal has no dedicated SET-UP key.
        push_byte(out, 0xFE);
        return true;
    case SDLK_F1:
        if (allow_dec_setup_keys) {
            push_byte(out, 0x1B);
            push_byte(out, 0x14);
            return true;
        }
        return false;
    case SDLK_F2:
        if (allow_dec_setup_keys) {
            push_byte(out, 0x1B);
            push_byte(out, 0x1A);
            return true;
        }
        return false;
    case SDLK_F3:
        if (allow_dec_setup_keys) {
            push_byte(out, 0x1B);
            push_byte(out, 0x1C);
            return true;
        }
        return false;
    case SDLK_F4:
        if (allow_dec_setup_keys) {
            push_byte(out, 0x1B);
            push_byte(out, 0x16);
            return true;
        }
        return false;
    case SDLK_SCROLLLOCK:
        if (allow_dec_setup_keys) {
            push_byte(out, 0xB0);
            return true;
        }
        return false;
    default:
        return false;
    }
}
