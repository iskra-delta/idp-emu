#pragma once

#include <imgui.h>
#include <SDL.h>

void draw_custom_title_bar(SDL_Window* window);
bool custom_title_close_hit(SDL_Window* window, int event_x, int event_y);
int custom_title_menu_hit(int event_x, int event_y);
ImVec2 custom_title_menu_pos(int index);
