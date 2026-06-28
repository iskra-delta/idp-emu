#include "panel_disasm.hpp"
#include "../partner.hpp"
#include "z80dasm.h"
#include <imgui.h>
#include <cstring>
#include <cstdio>
#include <string>

namespace {

struct dasm_context
{
    const partner *emu;
    uint16_t addr;
    char buf[64];
    int buf_pos;
};

uint8_t dasm_input(void *user_data)
{
    auto *ctx = static_cast<dasm_context *>(user_data);
    return ctx->emu->peek_mem(ctx->addr++);
}

void dasm_output(char c, void *user_data)
{
    auto *ctx = static_cast<dasm_context *>(user_data);
    if (ctx->buf_pos < 63)
        ctx->buf[ctx->buf_pos++] = c;
}

uint8_t op8(const partner &emu, uint16_t addr)
{
    return emu.peek_mem(addr);
}

const char *partner_port_name(uint8_t port)
{
    if (port >= 0x10 && port <= 0x12) return "SASI HDD (Xebec S1410)";
    if (port >= 0x30 && port <= 0x33) return "GDP/AVDC shared control";
    if (port >= 0x34 && port <= 0x3F) return "SCN2674 AVDC text";
    if (port == 0x80) return "Memory: disable EPROM";
    if (port == 0x88) return "Memory: RAM bank 1";
    if (port == 0x90) return "Memory: RAM bank 2";
    if (port >= 0xA0 && port <= 0xA7) return "RTC MM58167A time";
    if (port >= 0xA8 && port <= 0xAF) return "RTC MM58167A alarm/NVRAM";
    if (port >= 0xB0 && port <= 0xB6) return "RTC MM58167A control/status";
    if (port == 0xBF) return "RTC MM58167A test";
    if (port == 0x98) return "FDC motor/status";
    if (port == 0xC0) return "Z80 DMA register";
    if (port >= 0xD0 && port <= 0xD3) return "Z80 PIO";
    if (port >= 0xD8 && port <= 0xDB) return "Z80 SIO channel 1";
    if (port >= 0xE0 && port <= 0xE4) return "Z80 SIO channel 2";
    if (port == 0xE8) return "FDC interrupt vector";
    if (port == 0xF0) return "i8272 FDC status";
    if (port == 0xF1) return "i8272 FDC data";
    return "Unknown/undocumented";
}

static void service_bus(const partner &emu, uint64_t &pins)
{
    if ((pins & Z80_MREQ) && (pins & Z80_RD))
    {
        Z80_SET_DATA(pins, emu.peek_mem(Z80_GET_ADDR(pins)));
    }
    else if ((pins & Z80_IORQ) && (pins & Z80_RD))
    {
        // For timing estimation in disasm view, return open-bus style data.
        Z80_SET_DATA(pins, 0xFF);
    }
}

int simulate_cycles(const partner &emu, uint16_t pc, int max_ticks = 512)
{
    z80_t cpu = emu.get_cpu();
    uint64_t pins = z80_prefetch(&cpu, pc);
    int ticks = 0;

    while (z80_opdone(&cpu) && ticks < max_ticks)
    {
        pins = z80_tick(&cpu, pins);
        ticks++;
        service_bus(emu, pins);
    }
    while (!z80_opdone(&cpu) && ticks < max_ticks)
    {
        pins = z80_tick(&cpu, pins);
        ticks++;
        service_bus(emu, pins);
    }
    if (ticks >= max_ticks)
        return -1;
    return ticks;
}

int cycle_value(const partner &emu, uint16_t pc)
{
    return simulate_cycles(emu, pc);
}

std::string io_comment(const partner &emu, uint16_t pc)
{
    uint8_t b0 = op8(emu, pc);
    uint8_t b1 = op8(emu, pc + 1);

    if (b0 == 0xD3 || b0 == 0xDB)
    {
        uint8_t port = b1;
        char buf[128];
        snprintf(buf, sizeof(buf), "; %s %02Xh -> %s",
                 (b0 == 0xDB) ? "IN" : "OUT", port, partner_port_name(port));
        return std::string(buf);
    }

    if (b0 == 0xED)
    {
        // IN r,(C)
        if ((b1 & 0xC7) == 0x40)
        {
            uint8_t port = emu.get_cpu().c;
            char buf[128];
            snprintf(buf, sizeof(buf), "; IN (C=%02Xh) -> %s", port, partner_port_name(port));
            return std::string(buf);
        }
        // OUT (C),r
        if ((b1 & 0xC7) == 0x41)
        {
            uint8_t port = emu.get_cpu().c;
            char buf[128];
            snprintf(buf, sizeof(buf), "; OUT (C=%02Xh) -> %s", port, partner_port_name(port));
            return std::string(buf);
        }
        // Block I/O ops use port in C
        if (b1 == 0xA2 || b1 == 0xAA || b1 == 0xB2 || b1 == 0xBA)
        {
            uint8_t port = emu.get_cpu().c;
            char buf[128];
            snprintf(buf, sizeof(buf), "; IN* (C=%02Xh) -> %s", port, partner_port_name(port));
            return std::string(buf);
        }
        if (b1 == 0xA3 || b1 == 0xAB || b1 == 0xB3 || b1 == 0xBB)
        {
            uint8_t port = emu.get_cpu().c;
            char buf[128];
            snprintf(buf, sizeof(buf), "; OUT* (C=%02Xh) -> %s", port, partner_port_name(port));
            return std::string(buf);
        }
    }

    return {};
}

} // anonymous namespace

void panels::render_disasm(partner &emu, bool &paused, dbg_action &action, bool *p_open)
{
    if (!ImGui::Begin("Disassembly", p_open,
                      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::End();
        return;
    }

    // Debug toolbar
    if (paused)
    {
        if (ImGui::Button("Run (Ctrl+F9)"))
            paused = false;
        ImGui::SameLine();
        if (ImGui::Button("Step Into (F11)"))
            action = dbg_action::STEP_INTO;
        ImGui::SameLine();
        if (ImGui::Button("Step Over (F10)"))
            action = dbg_action::STEP_OVER;
    }
    else
    {
        if (ImGui::Button("Pause (Ctrl+F9)"))
            paused = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset"))
    {
        emu.reset();
        paused = true;
    }

    ImGui::Separator();

    // Disassembly listing
    uint16_t pc = emu.get_current_pc();
    uint16_t addr = pc;

    for (int i = 0; i < 32; i++)
    {
        dasm_context ctx;
        ctx.emu = &emu;
        ctx.addr = addr;
        ctx.buf_pos = 0;
        memset(ctx.buf, 0, sizeof(ctx.buf));

        uint16_t start = addr;
        uint16_t next_addr = z80dasm_op(addr, dasm_input, dasm_output, &ctx);
        ctx.buf[ctx.buf_pos] = '\0';

        char hex[16] = {};
        int hex_pos = 0;
        for (uint16_t a = start; a < next_addr && hex_pos < 14; a++)
            hex_pos += snprintf(hex + hex_pos, sizeof(hex) - hex_pos,
                                "%02X ", emu.peek_mem(a));

        bool is_current = (start == pc);
        if (is_current)
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.0f, 1.0f));

        int cycles = cycle_value(emu, start);
        char cycbuf[3] = {'?', '?', '\0'};
        if (cycles >= 0 && cycles <= 99)
            snprintf(cycbuf, sizeof(cycbuf), "%02d", cycles);
        std::string io = io_comment(emu, start);
        ImGui::Text("%04X: %-12s %-16s [%s] %s",
                    start, hex, ctx.buf, cycbuf, io.c_str());

        if (is_current)
        {
            ImGui::PopStyleColor();
            ImGui::SetScrollHereY(0.25f);
        }

        addr = next_addr;
    }

    ImGui::End();
}
