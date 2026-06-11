#include "custom_title_bar.hpp"

#include "chrome_metrics.hpp"
#include "chrome_theme.hpp"

#include <imgui.h>
#include <SDL.h>

namespace {
constexpr int k_menu_count = 3;
const char* k_menu_labels[k_menu_count] = {"Emulation", "View", "Devices"};

struct menu_rect {
    float x;
    float w;
};

menu_rect g_menu_rects[k_menu_count]{};

bool mouse_pos_in_window(SDL_Window* window, int& mouse_x, int& mouse_y)
{
    mouse_x = 0;
    mouse_y = 0;
    if (window == nullptr || SDL_GetMouseFocus() != window) {
        return false;
    }
    SDL_GetMouseState(&mouse_x, &mouse_y);
    return true;
}

void close_button_rect(SDL_Window* window, float& bx, float& by, float& bsize)
{
    int win_w = 0;
    int win_h = 0;
    SDL_GetWindowSize(window, &win_w, &win_h);
    (void)win_h;
    bsize = chrome_metrics::title_bar_height - 4.0f;
    bx = static_cast<float>(win_w) - bsize - 4.0f;
    by = 2.0f;
}

ImU32 dim_alpha(ImU32 color, float factor)
{
    const ImU32 a = (color >> 24) & 0xFF;
    return (color & 0x00FFFFFFu) | (static_cast<ImU32>(a * factor) << 24);
}
} // namespace

bool custom_title_close_hit(SDL_Window* window, int event_x, int event_y)
{
    float bx = 0.0f;
    float by = 0.0f;
    float bsize = 0.0f;
    close_button_rect(window, bx, by, bsize);

    return event_x >= static_cast<int>(bx) &&
           event_x <= static_cast<int>(bx + bsize) &&
           event_y >= static_cast<int>(by) &&
           event_y <= static_cast<int>(by + bsize);
}

int custom_title_menu_hit(int event_x, int event_y)
{
    if (event_y < 0 || event_y >= static_cast<int>(chrome_metrics::title_bar_height)) {
        return -1;
    }
    for (int i = 0; i < k_menu_count; ++i) {
        if (event_x >= static_cast<int>(g_menu_rects[i].x) &&
            event_x <= static_cast<int>(g_menu_rects[i].x + g_menu_rects[i].w)) {
            return i;
        }
    }
    return -1;
}

ImVec2 custom_title_menu_pos(int index)
{
    ImGuiViewport* vp = ImGui::GetMainViewport();
    return {vp->Pos.x + g_menu_rects[index].x,
            vp->Pos.y + chrome_metrics::title_bar_height + 1.0f};
}

void draw_custom_title_bar(SDL_Window* window)
{
    float cbx = 0.0f;
    float cby = 0.0f;
    float cbsize = 0.0f;
    close_button_rect(window, cbx, cby, cbsize);

    int win_w = 0;
    int win_h = 0;
    SDL_GetWindowSize(window, &win_w, &win_h);
    (void)win_h;
    const float w = static_cast<float>(win_w);
    const float h = chrome_metrics::title_bar_height;

    ImGuiViewport* vp = ImGui::GetMainViewport();
    const float ox = vp->Pos.x;
    const float oy = vp->Pos.y;

    ImDrawList* dl = ImGui::GetForegroundDrawList(vp);
    const float fs = ImGui::GetFontSize();
    ImFont* font = ImGui::GetFont();
    const float mid_y = oy + (h - fs) * 0.5f;

    dl->AddRectFilled({ox, oy}, {ox + w, oy + h}, chrome_theme().title_bg);

    const char* app_name = SDL_GetWindowTitle(window);
    if (!app_name || !app_name[0]) {
        app_name = "IDP Emulator";
    }
    dl->AddText(font, fs,
                {ox + chrome_metrics::title_text_offset_x, mid_y},
                dim_alpha(chrome_theme().title_text, 0.55f), app_name);

    const ImVec2 app_name_size = ImGui::CalcTextSize(app_name);
    const float separator_x = chrome_metrics::title_text_offset_x + app_name_size.x + 8.0f;
    dl->AddText(font, fs, {ox + separator_x, mid_y},
                dim_alpha(chrome_theme().title_text, 0.30f), "|");

    int mouse_x = 0;
    int mouse_y = 0;
    const bool mouse_over_main_window = mouse_pos_in_window(window, mouse_x, mouse_y);
    const int hovered_menu =
        mouse_over_main_window ? custom_title_menu_hit(mouse_x, mouse_y) : -1;

    const ImVec2 separator_size = ImGui::CalcTextSize("|");
    constexpr float pad = 6.0f;
    float cursor_x = separator_x + separator_size.x + 8.0f;

    for (int i = 0; i < k_menu_count; ++i) {
        ImVec2 label_size = ImGui::CalcTextSize(k_menu_labels[i]);
        g_menu_rects[i] = {cursor_x, label_size.x + pad * 2.0f};

        if (hovered_menu == i) {
            dl->AddRectFilled(
                {ox + cursor_x, oy + 2.0f},
                {ox + cursor_x + label_size.x + pad * 2.0f, oy + h - 2.0f},
                chrome_theme().close_hover_bg,
                3.0f);
        }

        dl->AddText(font, fs,
                    {ox + cursor_x + pad, mid_y},
                    chrome_theme().title_text,
                    k_menu_labels[i]);

        cursor_x += label_size.x + pad * 2.0f + 2.0f;
    }

    if (mouse_over_main_window && custom_title_close_hit(window, mouse_x, mouse_y)) {
        dl->AddRectFilled({ox + cbx, oy + cby},
                          {ox + cbx + cbsize, oy + cby + cbsize},
                          chrome_theme().close_hover_bg,
                          3.0f);
    }

    const float pad_x = cbsize * 0.28f;
    dl->AddLine({ox + cbx + pad_x, oy + cby + pad_x},
                {ox + cbx + cbsize - pad_x, oy + cby + cbsize - pad_x},
                chrome_theme().close_x_color,
                1.5f);
    dl->AddLine({ox + cbx + cbsize - pad_x, oy + cby + pad_x},
                {ox + cbx + pad_x, oy + cby + cbsize - pad_x},
                chrome_theme().close_x_color,
                1.5f);
}
