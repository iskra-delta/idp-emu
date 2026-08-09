#include "terminal_keymap.hpp"

namespace {

static inline void push_byte(std::vector<uint8_t>& out, uint8_t value)
{
    out.push_back(value);
}

}

bool terminal_key_repeat_limiter::accept(SDL_Keycode key, bool repeat,
                                         uint32_t timestamp_ms,
                                         bool repeat_enabled)
{
    if (!repeat) {
        last_repeat_ms_[key] = timestamp_ms;
        return true;
    }
    if (!repeat_enabled) {
        return false;
    }

    auto [it, inserted] = last_repeat_ms_.emplace(key, timestamp_ms);
    if (inserted) {
        return true;
    }
    if ((uint32_t)(timestamp_ms - it->second) < interval_ms) {
        return false;
    }
    it->second = timestamp_ms;
    return true;
}

void terminal_key_repeat_limiter::release(SDL_Keycode key)
{
    last_repeat_ms_.erase(key);
}

void terminal_key_repeat_limiter::clear()
{
    last_repeat_ms_.clear();
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

bool map_terminal_key(SDL_Keycode key,
                      std::vector<uint8_t>& out,
                      terminal_profile profile,
                      bool allow_dec_setup_keys)
{
    const auto push_cursor_key = [&](uint8_t final_byte) {
        push_byte(out, 0x1B);
        if (profile == terminal_profile::vt100_ansi) {
            push_byte(out, '[');
        }
        push_byte(out, final_byte);
    };

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
        push_cursor_key('A');
        return true;
    case SDLK_DOWN:
        push_cursor_key('B');
        return true;
    case SDLK_RIGHT:
        push_cursor_key('C');
        return true;
    case SDLK_LEFT:
        push_cursor_key('D');
        return true;
    case SDLK_DELETE:
        push_byte(out, 0x7F);
        return true;
    case SDLK_PAUSE:
    case SDLK_F12:
        // PartOS BIOS waits for raw 0xFE during the early setup window on
        // both CRT and GDP models. Keep dedicated host shortcuts for setup
        // while leaving the normal Delete key available for shell editing.
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
