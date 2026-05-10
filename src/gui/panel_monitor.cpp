#include "panel_monitor.hpp"
#include "display.hpp"
#include <imgui.h>

void panels::render_monitor(display &disp, bool *p_open)
{
    if (!ImGui::Begin("Monitor", p_open,
                      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::End();
        return;
    }

    const auto mode = disp.get_phosphor_type();
    if (ImGui::BeginCombo("Monitor Type",
                          mode == display::phosphor_type::green ? "Green CRT" :
                          mode == display::phosphor_type::orange ? "Orange CRT" :
                          mode == display::phosphor_type::color ? "Color CRT" :
                          "LCD"))
    {
        if (ImGui::Selectable("Green CRT", mode == display::phosphor_type::green))
            disp.set_phosphor_type(display::phosphor_type::green);
        if (ImGui::Selectable("Orange CRT", mode == display::phosphor_type::orange))
            disp.set_phosphor_type(display::phosphor_type::orange);
        if (ImGui::Selectable("Color CRT", mode == display::phosphor_type::color))
            disp.set_phosphor_type(display::phosphor_type::color);
        if (ImGui::Selectable("LCD", mode == display::phosphor_type::lcd))
            disp.set_phosphor_type(display::phosphor_type::lcd);
        ImGui::EndCombo();
    }

    float brightness = disp.get_monitor_brightness();
    float contrast = disp.get_monitor_contrast();
    float bloom = disp.get_monitor_bloom();
    float scanline = disp.get_monitor_scanline_strength();
    float mask = disp.get_monitor_mask_strength();
    float vignette = disp.get_monitor_vignette();
    float persistence = disp.get_monitor_persistence();

    if (ImGui::SliderFloat("Brightness", &brightness, 0.35f, 2.20f, "%.2f"))
        disp.set_monitor_brightness(brightness);
    if (ImGui::SliderFloat("Contrast", &contrast, 0.40f, 2.30f, "%.2f"))
        disp.set_monitor_contrast(contrast);
    if (ImGui::SliderFloat("Bloom", &bloom, 0.00f, 2.40f, "%.2f"))
        disp.set_monitor_bloom(bloom);
    if (ImGui::SliderFloat("Scanline Strength", &scanline, 0.00f, 1.60f, "%.2f"))
        disp.set_monitor_scanline_strength(scanline);
    if (ImGui::SliderFloat("Mask Strength", &mask, 0.00f, 1.60f, "%.2f"))
        disp.set_monitor_mask_strength(mask);
    if (ImGui::SliderFloat("Vignette", &vignette, 0.00f, 1.60f, "%.2f"))
        disp.set_monitor_vignette(vignette);
    if (ImGui::SliderFloat("Persistence", &persistence, 0.20f, 1.15f, "%.2f"))
        disp.set_monitor_persistence(persistence);

    if (ImGui::Button("Reset Monitor Defaults"))
        disp.reset_monitor_tuning();

    ImGui::End();
}
