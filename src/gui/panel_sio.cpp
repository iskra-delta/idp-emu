#include "panel_sio.hpp"
#include "../partner.hpp"
#include <imgui.h>

namespace {

const char *int_state_name(uint8_t s)
{
    if (s == 0) return "IDLE";
    if (s & Z80SIO_INT_SERVICED) return "SERV";
    if (s & Z80SIO_INT_REQUESTED) return "REQ";
    if (s & Z80SIO_INT_NEEDED) return "PEND";
    return "?";
}

void render_channel(const char *name, const z80sio_channel_t &ch)
{
    ImGui::SeparatorText(name);
    ImGui::Text("RX:%02X %s   TX:%02X %s",
                ch.rx_data, ch.rx_ready ? "RDY" : "---",
                ch.tx_data, ch.tx_ready ? "RDY" : "BUSY");
    ImGui::Text("WR0:%02X WR1:%02X WR2:%02X WR3:%02X",
                ch.wr[0], ch.wr[1], ch.wr[2], ch.wr[3]);
    ImGui::Text("WR4:%02X WR5:%02X WR6:%02X WR7:%02X",
                ch.wr[4], ch.wr[5], ch.wr[6], ch.wr[7]);
    ImGui::Text("RR0:%02X RR1:%02X RR2:%02X IDX:%u",
                ch.rr[0], ch.rr[1], ch.rr[2], (unsigned)ch.reg_index);
    ImGui::Text("INT:%s (%02X) VEC:%02X",
                int_state_name(ch.int_state), ch.int_state, ch.int_vector);
    ImGui::Text("MODEM DCD:%d CTS:%d RTS:%d DTR:%d",
                ch.dcd ? 1 : 0, ch.cts ? 1 : 0, ch.rts ? 1 : 0, ch.dtr ? 1 : 0);
    ImGui::Text("ERR BRK:%d UND:%d PAR:%d OVR:%d FRM:%d",
                ch.break_abort ? 1 : 0, ch.tx_underrun ? 1 : 0,
                ch.parity_error ? 1 : 0, ch.rx_overrun ? 1 : 0, ch.framing_error ? 1 : 0);
}

} // namespace

void panels::render_sio(partner &emu)
{
    ImGui::Begin("Z80 SIO", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoCollapse);

    const z80sio_t &sio = emu.get_sio();

    ImGui::Text("Pins: %016llX", (unsigned long long)sio.pins);
    render_channel("Channel A", sio.chn[Z80SIO_CHANNEL_A]);
    render_channel("Channel B", sio.chn[Z80SIO_CHANNEL_B]);

    ImGui::End();
}
