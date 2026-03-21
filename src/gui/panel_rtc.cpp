#include "panel_rtc.hpp"
#include "../partner.hpp"
#include <imgui.h>

namespace {

int bcd_to_int(uint8_t bcd)
{
    return ((bcd >> 4) & 0x0F) * 10 + (bcd & 0x0F);
}

void draw_reg_row(const mm58167a_t &rtc, int base, int count)
{
    for (int i = 0; i < count; i++)
    {
        int r = base + i;
        ImGui::Text("%02X:%02X", r, rtc.regs[r]);
        if (i != count - 1)
            ImGui::SameLine();
    }
}

} // namespace

void panels::render_rtc(partner &emu)
{
    ImGui::Begin("MM58167 RTC", nullptr,
                 ImGuiWindowFlags_NoCollapse);

    const mm58167a_t &rtc = emu.get_rtc();

    int sec = bcd_to_int(rtc.regs[0x02]);
    int min = bcd_to_int(rtc.regs[0x03]);
    int hour = bcd_to_int(rtc.regs[0x04]);
    int day = bcd_to_int(rtc.regs[0x06]);
    int month = bcd_to_int(rtc.regs[0x07]);
    int year = bcd_to_int(rtc.regs[0x09]);
    int wday = rtc.regs[0x05] & 0x07;

    ImGui::Text("Time: %02d:%02d:%02d", hour, min, sec);
    ImGui::Text("Date: %02d/%02d/%02d  WDay:%d", day, month, year, wday);
    ImGui::Separator();

    ImGui::Text("A0-A7 (time):");
    draw_reg_row(rtc, 0x00, 8);
    ImGui::Text("A8-AF (alarm/NVRAM):");
    draw_reg_row(rtc, 0x08, 8);
    ImGui::Text("B0-B7 (status/control):");
    draw_reg_row(rtc, 0x10, 8);
    ImGui::Text("B8-BF (misc/test):");
    draw_reg_row(rtc, 0x18, 8);

    ImGui::End();
}
