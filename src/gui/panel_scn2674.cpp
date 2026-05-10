#include "panel_scn2674.hpp"
#include "../partner.hpp"
#include "../partner_gdp.hpp"
#include <imgui.h>

namespace {

void draw_ir_row(const scn2674_t &avdc, int base, int count)
{
    for (int i = 0; i < count; i++)
    {
        const int idx = base + i;
        ImGui::Text("%X:%02X", idx, avdc.ir[idx & 0x0F]);
        if (i != count - 1)
            ImGui::SameLine();
    }
}

} // namespace

void panels::render_scn2674(partner &emu, bool *p_open)
{
    if (!ImGui::Begin("SCN2674 AVDC", p_open,
                      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::End();
        return;
    }

    const auto *gdp = dynamic_cast<const partner_gdp*>(&emu);
    if (!gdp)
    {
        ImGui::TextUnformatted("Available for GDP machine only.");
        ImGui::End();
        return;
    }

    const scn2674_t &avdc = gdp->get_avdc();

    ImGui::Text("Mode: display=%s gfx=%s cursor=%s rowtbl=%s",
                avdc.display_enabled ? "ON" : "OFF",
                avdc.gfx_enabled ? "ON" : "OFF",
                avdc.cursor_enabled ? "ON" : "OFF",
                avdc.use_row_table ? "ON" : "OFF");

    ImGui::Text("Config: rows=%u cols=%u scanlines/char=%u scroll_lines=%u",
                (unsigned)avdc.rows_per_screen,
                (unsigned)avdc.chars_per_row,
                (unsigned)avdc.scanlines_per_char_row,
                (unsigned)avdc.scroll_lines);

    ImGui::Text("Pointers: start1=%04X start2=%04X start2_start=%04X",
                avdc.start1_addr & 0x3FFFu,
                avdc.start2_addr & 0x3FFFu,
                avdc.start2_addr_start & 0x3FFFu);
    ImGui::Text("Cursor/Display: cursor=%04X display_ptr=%04X addr_latch=%04X",
                avdc.cursor_addr & 0x3FFFu,
                avdc.display_ptr_addr & 0x3FFFu,
                avdc.addr_latch & 0x3FFFu);

    ImGui::Text("Status: ST=%02X IRQ=%02X CMD=%02X IR_PTR=%u busy=%u",
                avdc.status, avdc.irq_status, avdc.cmd,
                (unsigned)avdc.ir_ptr, (unsigned)avdc.busy_ticks);
    ImGui::Text("Split: useS2=[%d,%d] split=[%u,%u] start=%d end=%d",
                avdc.split_use_screen2[0] ? 1 : 0,
                avdc.split_use_screen2[1] ? 1 : 0,
                (unsigned)avdc.split_register[0],
                (unsigned)avdc.split_register[1],
                avdc.scroll_start ? 1 : 0,
                avdc.scroll_end ? 1 : 0);
    ImGui::Text("Buffers: first=%04X last_nibble=%X glyph_rom=%s",
                avdc.display_buffer_first_addr & 0x0FFFu,
                avdc.display_buffer_last_nibble & 0x0Fu,
                avdc.glyph_rom_loaded ? "loaded" : "none");

    ImGui::Separator();
    ImGui::TextUnformatted("IR (0x0..0xF):");
    draw_ir_row(avdc, 0x0, 8);
    draw_ir_row(avdc, 0x8, 8);

    // Row table and VRAM content for last 4 rows (setup rows area).
    ImGui::Separator();
    const int rows = (int)avdc.rows_per_screen;
    const int cols = (int)avdc.chars_per_row;
    const uint16_t rowtbl = avdc.start2_addr & 0x3FFFu;
    ImGui::Text("Row table @ %04X  cols=%d  rows=%d", rowtbl, cols, rows);
    const int first_show = std::max(0, rows - 4);
    for (int r = first_show; r < rows && r < 26; r++)
    {
        // Read row table entry (little-endian).
        const uint16_t p = (uint16_t)((rowtbl + r * 2) & 0x3FFFu);
        const uint16_t entry = (uint16_t)(avdc.vram[p] | ((uint16_t)avdc.vram[(p+1)&0x3FFFu] << 8));
        const uint16_t addr = entry & 0x3FFFu;
        // Read first 40 chars of this row from VRAM.
        char buf[41]{};
        for (int c = 0; c < 40 && c < cols; c++)
        {
            const uint8_t ch = avdc.vram[(addr + c) & 0x3FFFu];
            buf[c] = (ch >= 0x20 && ch < 0x7F) ? (char)ch : '.';
        }
        buf[40] = '\0';
        ImGui::Text("row%02d tbl=%04X addr=%04X \"%s\"", r, p, addr, buf);
    }

    ImGui::End();
}
