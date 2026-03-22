#include "panel_regs.hpp"
#include "../partner.hpp"
#include <imgui.h>

void panels::render_registers(partner &emu, bool *p_open)
{
    if (!ImGui::Begin("Z80 Registers", p_open,
                      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::End();
        return;
    }

    const z80_t &cpu = emu.get_cpu();

    ImGui::Text("PC: %04X   SP: %04X", emu.get_current_pc(), cpu.sp);
    ImGui::Text("AF: %04X   AF': %04X", cpu.af, cpu.af2);
    ImGui::Text("BC: %04X   BC': %04X", cpu.bc, cpu.bc2);
    ImGui::Text("DE: %04X   DE': %04X", cpu.de, cpu.de2);
    ImGui::Text("HL: %04X   HL': %04X", cpu.hl, cpu.hl2);
    ImGui::Text("IX: %04X   IY: %04X", cpu.ix, cpu.iy);
    ImGui::Text("I:  %02X     R:  %02X", cpu.i, cpu.r);
    ImGui::Text("IM: %d   IFF1: %d  IFF2: %d", cpu.im, cpu.iff1 ? 1 : 0, cpu.iff2 ? 1 : 0);

    ImGui::Separator();
    ImGui::Text("Flags: %c%c%c%c%c%c%c%c",
                (cpu.f & Z80_SF) ? 'S' : '-',
                (cpu.f & Z80_ZF) ? 'Z' : '-',
                (cpu.f & Z80_YF) ? 'Y' : '-',
                (cpu.f & Z80_HF) ? 'H' : '-',
                (cpu.f & Z80_XF) ? 'X' : '-',
                (cpu.f & Z80_PF) ? 'P' : '-',
                (cpu.f & Z80_NF) ? 'N' : '-',
                (cpu.f & Z80_CF) ? 'C' : '-');

    ImGui::Separator();
    ImGui::Text("Ticks: %llu", (unsigned long long)emu.get_tick_count());
    ImGui::Text("ROM: %s   Bank: %d",
                emu.is_rom_enabled() ? "ON" : "OFF", emu.get_ram_bank());

    const uint64_t pins = emu.get_pins();
    ImGui::Text("Pins: %s %s %s %s %s",
                (pins & Z80_HALT) ? "HALT" : "----",
                (pins & Z80_INT) ? "INT" : "---",
                (pins & Z80_M1) ? "M1" : "--",
                (pins & Z80_IORQ) ? "IORQ" : "----",
                (pins & Z80_IEIO) ? "IEIO" : "----");

    ImGui::End();
}
