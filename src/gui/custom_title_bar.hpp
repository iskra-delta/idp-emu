#pragma once

#include <imgui.h>
#include <SDL.h>

int draw_custom_title_bar(SDL_Window* window, int active_menu = -1);
bool custom_title_close_hit(SDL_Window* window, int event_x, int event_y);
int custom_title_menu_hit(int event_x, int event_y);
ImVec2 custom_title_menu_pos(int index);
