#include "chrome_theme.hpp"

#include <imgui.h>

namespace {
const chrome_theme_colors k_green = {
    "BlueprintGreen",
    {0.05f, 0.30f, 0.38f},
    IM_COL32(10, 68, 84, 255),
    IM_COL32(230, 247, 255, 255),
    IM_COL32(40, 140, 170, 255),
    IM_COL32(210, 240, 255, 220),
    IM_COL32(255, 255, 255, 100),
    IM_COL32(255, 255, 255, 80),
    IM_COL32(255, 255, 255, 22),
    IM_COL32(255, 255, 255, 50),
};

chrome_theme_colors g_colors = k_green;

void apply_imgui_colors(ImVec4 bg0, ImVec4 bg1, ImVec4 bg2, ImVec4 bg3,
                        ImVec4 accent, ImVec4 text, ImVec4 text_dim)
{
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* c = style.Colors;

    c[ImGuiCol_Text] = text;
    c[ImGuiCol_TextDisabled] = text_dim;

    c[ImGuiCol_WindowBg] = bg1;
    c[ImGuiCol_ChildBg] = bg1;
    c[ImGuiCol_PopupBg] = bg0;

    c[ImGuiCol_Border] = ImVec4(1.0f, 1.0f, 1.0f, 0.39f);
    c[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

    c[ImGuiCol_FrameBg] = bg2;
    c[ImGuiCol_FrameBgHovered] = bg3;
    c[ImGuiCol_FrameBgActive] = accent;

    c[ImGuiCol_TitleBg] = bg0;
    c[ImGuiCol_TitleBgActive] = bg0;
    c[ImGuiCol_TitleBgCollapsed] = bg0;

    c[ImGuiCol_MenuBarBg] = bg1;

    c[ImGuiCol_ScrollbarBg] = bg0;
    c[ImGuiCol_ScrollbarGrab] = bg3;
    c[ImGuiCol_ScrollbarGrabHovered] = accent;
    c[ImGuiCol_ScrollbarGrabActive] = accent;

    c[ImGuiCol_CheckMark] = text;
    c[ImGuiCol_SliderGrab] = bg3;
    c[ImGuiCol_SliderGrabActive] = accent;

    c[ImGuiCol_Button] = bg2;
    c[ImGuiCol_ButtonHovered] = bg3;
    c[ImGuiCol_ButtonActive] = accent;

    c[ImGuiCol_Header] = bg2;
    c[ImGuiCol_HeaderHovered] = bg3;
    c[ImGuiCol_HeaderActive] = accent;

    c[ImGuiCol_Separator] = bg3;
    c[ImGuiCol_SeparatorHovered] = accent;
    c[ImGuiCol_SeparatorActive] = accent;

    c[ImGuiCol_ResizeGrip] = bg3;
    c[ImGuiCol_ResizeGripHovered] = accent;
    c[ImGuiCol_ResizeGripActive] = accent;

    c[ImGuiCol_Tab] = bg2;
    c[ImGuiCol_TabHovered] = accent;
    c[ImGuiCol_TabActive] = bg3;
    c[ImGuiCol_TabUnfocused] = bg1;
    c[ImGuiCol_TabUnfocusedActive] = bg2;

    c[ImGuiCol_DockingPreview] = accent;
    c[ImGuiCol_DockingEmptyBg] = bg0;

    c[ImGuiCol_PlotLines] = text_dim;
    c[ImGuiCol_PlotHistogram] = accent;

    c[ImGuiCol_TableHeaderBg] = bg2;
    c[ImGuiCol_TableBorderStrong] = bg3;
    c[ImGuiCol_TableBorderLight] = bg2;

    c[ImGuiCol_TextSelectedBg] = ImVec4(accent.x, accent.y, accent.z, 0.55f);
    c[ImGuiCol_NavHighlight] = accent;
}
} // namespace

const chrome_theme_colors& chrome_theme()
{
    return g_colors;
}

void apply_chrome_theme()
{
    g_colors = k_green;

    const ImVec4 bg0 = {10.0f / 255.0f, 68.0f / 255.0f, 84.0f / 255.0f, 1.0f};
    const ImVec4 bg1 = {14.0f / 255.0f, 86.0f / 255.0f, 106.0f / 255.0f, 1.0f};
    const ImVec4 bg2 = {20.0f / 255.0f, 108.0f / 255.0f, 130.0f / 255.0f, 1.0f};
    const ImVec4 bg3 = {28.0f / 255.0f, 130.0f / 255.0f, 155.0f / 255.0f, 1.0f};
    const ImVec4 accent = {50.0f / 255.0f, 170.0f / 255.0f, 200.0f / 255.0f, 1.0f};
    const ImVec4 text = {0.90f, 0.97f, 1.0f, 1.0f};
    const ImVec4 text_dim = {0.58f, 0.76f, 0.84f, 1.0f};

    apply_imgui_colors(bg0, bg1, bg2, bg3, accent, text, text_dim);
}
