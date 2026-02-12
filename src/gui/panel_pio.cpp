#include "panel_pio.hpp"
#include "../partner.hpp"
#include <imgui.h>

namespace {

const char *mode_name(uint8_t mode)
{
    switch (mode)
    {
    case Z80PIO_MODE_OUTPUT: return "OUTPUT";
    case Z80PIO_MODE_INPUT: return "INPUT";
    case Z80PIO_MODE_BIDIRECTIONAL: return "BIDIR";
    case Z80PIO_MODE_BITCONTROL: return "BITCTL";
    default: return "?";
    }
}

const char *int_state_name(uint8_t s)
{
    if (s == 0) return "IDLE";
    if (s & Z80PIO_INT_SERVICED) return "SERV";
    if (s & Z80PIO_INT_REQUESTED) return "REQ";
    if (s & Z80PIO_INT_NEEDED) return "PEND";
    return "?";
}

void render_port(const char *name, const z80pio_port_t &p)
{
    ImGui::SeparatorText(name);
    ImGui::Text("Mode:%s  Ready:%d  Input:%02X  Output:%02X",
                mode_name(p.mode), p.ready ? 1 : 0, p.input, p.output);
    ImGui::Text("IOSEL:%02X  MASK:%02X  CTRL:%02X  VEC:%02X",
                p.io_select, p.int_mask, p.int_control, p.int_vector);
    ImGui::Text("INT:%s (%02X) EN:%d MATCH:%d",
                int_state_name(p.int_state), p.int_state,
                p.int_enabled ? 1 : 0, p.bctrl_match ? 1 : 0);
    ImGui::Text("EXPECT IOSEL:%d MASK:%d STB_PREV:%d",
                p.expect_io_select ? 1 : 0,
                p.expect_int_mask ? 1 : 0,
                p.strobe_prev ? 1 : 0);
}

} // namespace

void panels::render_pio(partner &emu)
{
    ImGui::Begin("Z80 PIO", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoCollapse);

    const z80pio_t &pio = emu.get_pio();

    ImGui::Text("Pins: %016llX  Reset:%d",
                (unsigned long long)pio.pins, pio.reset_active ? 1 : 0);
    render_port("Port A", pio.port[Z80PIO_PORT_A]);
    render_port("Port B", pio.port[Z80PIO_PORT_B]);

    ImGui::End();
}
