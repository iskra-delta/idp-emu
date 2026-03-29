#include "window_chrome.hpp"

#include "chrome_metrics.hpp"
#include "chrome_theme.hpp"

#include <imgui.h>

namespace {
void draw_background_grid(float width, float height)
{
    ImGuiViewport* vp = ImGui::GetMainViewport();
    const float ox = vp->Pos.x;
    const float oy = vp->Pos.y;

    ImDrawList* dl = ImGui::GetBackgroundDrawList(vp);
    const float origin_y = chrome_metrics::title_bar_height;
    constexpr float cell_minor = 20.0f;
    constexpr float cell_major = 100.0f;

    for (float x = 0.0f; x <= width; x += cell_minor) {
        const bool is_major = (static_cast<int>(x + 0.5f) % static_cast<int>(cell_major)) == 0;
        dl->AddLine({ox + x, oy + origin_y}, {ox + x, oy + height},
                    is_major ? chrome_theme().grid_major : chrome_theme().grid_minor,
                    is_major ? 1.0f : 0.5f);
    }

    for (float y = origin_y; y <= height; y += cell_minor) {
        const bool is_major =
            (static_cast<int>((y - origin_y) + 0.5f) % static_cast<int>(cell_major)) == 0;
        dl->AddLine({ox, oy + y}, {ox + width, oy + y},
                    is_major ? chrome_theme().grid_major : chrome_theme().grid_minor,
                    is_major ? 1.0f : 0.5f);
    }
}

void draw_border(ImDrawList* dl, float ox, float oy, float width, float height)
{
    dl->AddRect({ox + 0.5f, oy + 0.5f},
                {ox + width - 0.5f, oy + height - 0.5f},
                chrome_theme().chrome_border,
                0.0f,
                0,
                1.0f);
}

void draw_move_handle(ImDrawList* dl, float ox, float oy)
{
    constexpr float dot_r = 1.5f;
    constexpr float h_gap = 4.0f;
    constexpr float v_gap = 4.0f;
    constexpr int cols = 2;
    constexpr int rows = 3;

    const float grid_h = (rows - 1) * v_gap + dot_r * 2.0f;
    const float start_y = (chrome_metrics::title_bar_height - grid_h) * 0.5f + dot_r;
    const float start_x = 6.0f + dot_r;

    for (int c = 0; c < cols; ++c) {
        for (int r = 0; r < rows; ++r) {
            const float cx = ox + start_x + c * h_gap;
            const float cy = oy + start_y + r * v_gap;
            dl->AddCircleFilled({cx, cy}, dot_r, chrome_theme().chrome_handle, 6);
        }
    }
}

void draw_resize_grip(ImDrawList* dl, float ox, float oy, float width, float height)
{
    const float zone = static_cast<float>(chrome_metrics::resize_corner);
    constexpr float margin = 3.0f;
    const float usable = zone - margin * 2.0f;
    constexpr int lines = 5;

    for (int i = 1; i <= lines; ++i) {
        const float s = margin + usable * static_cast<float>(i) / static_cast<float>(lines);
        dl->AddLine({ox + width - s, oy + height - margin},
                    {ox + width - margin, oy + height - s},
                    chrome_theme().chrome_handle,
                    1.0f);
    }
}
} // namespace

void draw_window_chrome()
{
    const ImGuiIO& io = ImGui::GetIO();
    const float width = io.DisplaySize.x;
    const float height = io.DisplaySize.y;

    draw_background_grid(width, height);

    ImGuiViewport* vp = ImGui::GetMainViewport();
    const float ox = vp->Pos.x;
    const float oy = vp->Pos.y;

    ImDrawList* dl = ImGui::GetForegroundDrawList(vp);
    draw_border(dl, ox, oy, width, height);
    draw_move_handle(dl, ox, oy);
    draw_resize_grip(dl, ox, oy, width, height);
}
