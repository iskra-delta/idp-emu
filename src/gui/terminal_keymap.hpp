#pragma once

#include "../terminal/terminal_factory.hpp"

#include <SDL.h>

#include <cstdint>
#include <unordered_map>
#include <vector>

class terminal_key_repeat_limiter
{
public:
    static constexpr uint32_t interval_ms = 100;

    bool accept(SDL_Keycode key, bool repeat, uint32_t timestamp_ms,
                bool repeat_enabled = true);
    void release(SDL_Keycode key);
    void clear();

private:
    std::unordered_map<SDL_Keycode, uint32_t> last_repeat_ms_{};
};

bool map_terminal_ctrl_key(SDL_Keycode key, SDL_Keymod mods, std::vector<uint8_t>& out);
bool map_terminal_key(SDL_Keycode key,
                      std::vector<uint8_t>& out,
                      terminal_profile profile,
                      bool allow_dec_setup_keys);
