#include "panel_disasm.hpp"
#include "../partner.hpp"
#include "z80dasm.h"
#include <imgui.h>
#include <cstring>
#include <cstdio>

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

} // anonymous namespace

void panels::render_disasm(partner &emu, bool &paused, dbg_action &action)
{
    ImGui::Begin("Disassembly", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoCollapse);

    // Debug toolbar
    if (paused)
    {
        if (ImGui::Button("Run (F5)"))
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
        if (ImGui::Button("Pause (F5)"))
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

        ImGui::Text("%04X: %-12s %s", start, hex, ctx.buf);

        if (is_current)
        {
            ImGui::PopStyleColor();
            ImGui::SetScrollHereY(0.25f);
        }

        addr = next_addr;
    }

    ImGui::End();
}
