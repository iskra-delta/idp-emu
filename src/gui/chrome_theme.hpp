#pragma once

#include <imgui.h>

struct chrome_theme_colors {
    const char* name;
    float gl_clear[3];

    ImU32 title_bg;
    ImU32 title_text;
    ImU32 close_hover_bg;
    ImU32 close_x_color;
    ImU32 chrome_border;
    ImU32 chrome_handle;
    ImU32 grid_minor;
    ImU32 grid_major;
};

const chrome_theme_colors& chrome_theme();
void apply_chrome_theme();
