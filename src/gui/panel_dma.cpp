#include "panel_dma.hpp"
#include "../partner.hpp"
#include <imgui.h>

namespace {

const char *state_name(z80dma_state_t s)
{
    switch (s)
    {
    case Z80DMA_STATE_IDLE: return "IDLE";
    case Z80DMA_STATE_WAIT_BUS: return "WAIT_BUS";
    case Z80DMA_STATE_READ: return "READ";
    case Z80DMA_STATE_WRITE: return "WRITE";
    case Z80DMA_STATE_SEARCH: return "SEARCH";
    case Z80DMA_STATE_VERIFY: return "VERIFY";
    default: return "?";
    }
}

const char *mode_name(z80dma_transfer_mode_t m)
{
    switch (m)
    {
    case Z80DMA_MODE_BYTE: return "BYTE";
    case Z80DMA_MODE_CONTINUOUS: return "CONT";
    case Z80DMA_MODE_BURST: return "BURST";
    default: return "?";
    }
}

const char *int_state_name(uint8_t s)
{
    if (s == 0) return "IDLE";
    if (s & Z80DMA_INT_SERVICED) return "SERV";
    if (s & Z80DMA_INT_REQUESTED) return "REQ";
    if (s & Z80DMA_INT_NEEDED) return "PEND";
    return "?";
}

void render_port(const char *name, const z80dma_port_t &p)
{
    ImGui::SeparatorText(name);
    ImGui::Text("Addr:%04X Start:%04X Len:%04X StartLen:%04X",
                p.address, p.start_address, p.block_length, p.start_length);
    ImGui::Text("Timing:%02X Mem:%d Inc:%d Dec:%d",
                p.timing, p.is_memory ? 1 : 0, p.increment ? 1 : 0, p.decrement ? 1 : 0);
}

} // namespace

void panels::render_dma(partner &emu, bool *p_open)
{
    if (!ImGui::Begin("Z80 DMA", p_open,
                      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::End();
        return;
    }

    const z80dma_t &dma = emu.get_dma();

    ImGui::Text("State:%s  Mode:%s  EN:%d  Dir:%s",
                state_name(dma.state), mode_name(dma.mode),
                dma.enabled ? 1 : 0, dma.direction_ab ? "A->B" : "B->A");
    ImGui::Text("Status:%02X  INT:%s (%02X) VEC:%02X",
                dma.status, int_state_name(dma.int_state), dma.int_state, dma.int_vector);
    ImGui::Text("Flags Search:%d AutoRestart:%d IRQ_EN:%d",
                dma.search_mode ? 1 : 0, dma.auto_restart ? 1 : 0, dma.interrupt_enable ? 1 : 0);
    ImGui::Text("Pulse:%02X Match:%02X Mask:%02X Latch:%02X",
                dma.pulse_control, dma.match_byte, dma.mask_byte, dma.data_latch);
    ImGui::Text("WR0:%02X WR1:%02X WR2:%02X WR3:%02X WR4:%02X WR5:%02X WR6:%02X",
                dma.wr[0], dma.wr[1], dma.wr[2], dma.wr[3], dma.wr[4], dma.wr[5], dma.wr[6]);
    ImGui::Text("CmdNeed:%u CmdRecv:%u Pins:%016llX",
                (unsigned)dma.cmd_bytes_needed, (unsigned)dma.cmd_bytes_received,
                (unsigned long long)dma.pins);

    render_port("Port A", dma.port_a);
    render_port("Port B", dma.port_b);

    ImGui::End();
}
