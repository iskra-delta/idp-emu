#include "panel_display.hpp"
#include "display.hpp"
#include "chrome_theme.hpp"
#include <imgui.h>
#include <array>
#include <cmath>

namespace {

// Must match the underscan/barrel constants in the CRT fragment shader
// (display.cpp) so the outline hugs the warped raster.
static inline ImVec2 crt_warp_uv(ImVec2 tc)
{
    tc.x = (tc.x - 0.5f) * 1.02f + 0.5f;
    tc.y = (tc.y - 0.5f) * 1.02f + 0.5f;
    const ImVec2 cc = ImVec2(tc.x - 0.5f, tc.y - 0.5f);
    const float r2 = cc.x * cc.x + cc.y * cc.y;
    const float k = 1.0f + 0.035f * r2 + 0.055f * r2 * r2;
    tc.x = cc.x * k + 0.5f;
    tc.y = cc.y * k + 0.5f;
    return tc;
}

static inline bool crt_uv_inside(ImVec2 tc)
{
    const ImVec2 uv = crt_warp_uv(tc);
    return uv.x >= 0.0f && uv.x <= 1.0f && uv.y >= 0.0f && uv.y <= 1.0f;
}

void add_curved_monitor_outline(ImDrawList *dl, const ImVec2 &min_p, const ImVec2 &max_p,
                                bool crt_curved, ImU32 color)
{
    const float w = max_p.x - min_p.x;
    const float h = max_p.y - min_p.y;
    if (w < 8.0f || h < 8.0f || !crt_curved)
    {
        dl->AddRect(min_p, max_p, color, 0.0f, 0, 1.0f);
        return;
    }

    constexpr int k_segments = 160;
    std::array<ImVec2, k_segments> pts{};
    const ImVec2 tc_center = ImVec2(0.5f, 0.5f);
    const float pi = 3.14159265358979323846f;

    for (int i = 0; i < k_segments; i++)
    {
        const float t = (2.0f * pi * (float)i) / (float)k_segments;
        const float ct = std::cos(t);
        const float st = std::sin(t);
        float lo = 0.0f;
        float hi = 1.2f;
        for (int it = 0; it < 22; it++)
        {
            const float mid = (lo + hi) * 0.5f;
            const ImVec2 tc = ImVec2(tc_center.x + ct * mid, tc_center.y + st * mid);
            if (crt_uv_inside(tc))
                lo = mid;
            else
                hi = mid;
        }

        // Tiny outward offset so the line sits just outside lit pixels.
        const float r = lo + 0.002f;
        const ImVec2 tc = ImVec2(tc_center.x + ct * r, tc_center.y + st * r);
        pts[(size_t)i] = ImVec2(min_p.x + tc.x * w, min_p.y + tc.y * h);
    }

    dl->AddPolyline(pts.data(), (int)pts.size(), color, ImDrawFlags_Closed, 1.0f);
}

} // namespace

void panels::render_display(display &disp, display_viewport_info *out_info)
{
    ImGui::Begin("Partner Display", nullptr,
                 ImGuiWindowFlags_NoCollapse |
                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImVec2 avail = ImGui::GetContentRegionAvail();
    const float aspect = disp.aspect_ratio();

    float img_w = avail.x;
    float img_h = avail.y;
    if (disp.preserve_aspect())
    {
        // Fit image maintaining aspect ratio and allow it to scale with the
        // docked window size.
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
        const float pad_x = (avail.x - img_w) * 0.5f;
        const float pad_y = (avail.y - img_h) * 0.5f;
        if (pad_x > 0)
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + pad_x);
        if (pad_y > 0)
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + pad_y);
    }

    const ImVec2 image_pos = ImGui::GetCursorScreenPos();
    ImGui::Image((ImTextureID)(intptr_t)disp.get_texture(),
                 {img_w, img_h},
                 ImVec2(0.0f, 0.0f),
                 ImVec2(disp.uv_max_x(), disp.uv_max_y()));
    const ImVec2 image_end = ImVec2(image_pos.x + img_w, image_pos.y + img_h);
    // Thin border around the monitor viewport, drawn in ImGui space so it
    // remains unaffected by the shader pass. Curved only for the analog CRT
    // modes; flat and LCD get a plain rectangle.
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const auto monitor_mode = disp.get_phosphor_type();
    const bool crt_curved =
        (monitor_mode == display::phosphor_type::green) ||
        (monitor_mode == display::phosphor_type::orange) ||
        (monitor_mode == display::phosphor_type::color) ||
        (monitor_mode == display::phosphor_type::retro_cool);
    add_curved_monitor_outline(dl, image_pos, image_end, crt_curved, chrome_theme().close_hover_bg);

    if (out_info)
    {
        out_info->x0 = image_pos.x;
        out_info->y0 = image_pos.y;
        out_info->x1 = image_end.x;
        out_info->y1 = image_end.y;
    }

    ImGui::End();
}
