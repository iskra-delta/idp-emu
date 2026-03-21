#include "panel_ef9367.hpp"
#include "../partner.hpp"
#include "../partner_gdp.hpp"
#include <imgui.h>

void panels::render_ef9367(partner &emu)
{
    ImGui::Begin("EF9367 GDP", nullptr,
                 ImGuiWindowFlags_NoCollapse);

    const auto *gdp = dynamic_cast<const partner_gdp*>(&emu);
    if (!gdp)
    {
        ImGui::TextUnformatted("Available for GDP machine only.");
        ImGui::End();
        return;
    }

    const ef9367_t &ef = gdp->get_ef9367();
    const int p_factor = ((ef.ch_size >> 4) & 0x0F) ? ((ef.ch_size >> 4) & 0x0F) : 16;
    const int q_factor = (ef.ch_size & 0x0F) ? (ef.ch_size & 0x0F) : 16;
    const int logical_h = ef.mode_512_lines ? 512 : 256;
    const uint8_t pa = gdp->get_gdp_pio_port_a();

    ImGui::Text("Mode: %s  logical=%dx%d",
                ef.mode_512_lines ? "1024x512" : "1024x256",
                1024, logical_h);
    ImGui::Text("Banks: read=%u write=%u scroll=%d",
                (unsigned)(ef.read_bank & 1u),
                (unsigned)(ef.write_bank & 1u),
                (int)ef.scroll_offset);
    ImGui::Text("Config: CHSZ=%02X (P=%d Q=%d)  GDP-PA=%02X",
                ef.ch_size, p_factor, q_factor, pa);
    ImGui::Text("Regs: CMD=%02X CR1=%02X CR2=%02X DX=%u DY=%u",
                ef.command, ef.cr1, ef.cr2,
                (unsigned)ef.dx, (unsigned)ef.dy);
    ImGui::Text("Coords: X=%u Y=%u  abs_phase=%u",
                (unsigned)ef.x, (unsigned)ef.y, (unsigned)ef.abs_phase);
    ImGui::Text("Status: ST=%02X ready=%d vblank=%d busy=%u scan=%u",
                ef.status, ef.ready ? 1 : 0, ef.vblank ? 1 : 0,
                (unsigned)ef.busy_ticks, (unsigned)ef.scan_ctr);
    ImGui::Text("Glyph ROM: %s", ef.glyph_rom_loaded ? "loaded" : "none");

    ImGui::End();
}
