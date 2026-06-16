#pragma once

#include <SDL.h>

#include <cstdint>
#include <vector>

bool map_terminal_ctrl_key(SDL_Keycode key, SDL_Keymod mods, std::vector<uint8_t>& out);
bool map_terminal_key(SDL_Keycode key, std::vector<uint8_t>& out, bool allow_dec_setup_keys);
