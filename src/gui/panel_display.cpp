#include "panel_display.hpp"
#include "display.hpp"
#include <imgui.h>

void panels::render_display(display &disp)
{
    ImGui::Begin("Partner Display", nullptr,
                 ImGuiWindowFlags_NoCollapse |
                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImVec2 avail = ImGui::GetContentRegionAvail();
    float aspect = disp.aspect_ratio();

    // Fit image maintaining aspect ratio and allow it to scale with the docked
    // window size.
    float img_w, img_h;
    if (avail.x / avail.y > aspect)
    {
        img_h = avail.y;
        img_w = img_h * aspect;
    }
    else
    {
        img_w = avail.x;
        img_h = img_w / aspect;
    }

    // Center in available space
    float pad_x = (avail.x - img_w) * 0.5f;
    float pad_y = (avail.y - img_h) * 0.5f;
    if (pad_x > 0)
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + pad_x);
    if (pad_y > 0)
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + pad_y);

    ImGui::Image((ImTextureID)(intptr_t)disp.get_texture(), {img_w, img_h});

    ImGui::End();
}
