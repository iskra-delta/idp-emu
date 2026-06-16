#include "panel_xebec.hpp"
#include "../partner.hpp"
#include <imgui.h>

namespace {

const char *phase_name(s1410_phase_t phase)
{
    switch (phase)
    {
    case S1410_PHASE_IDLE:         return "IDLE";
    case S1410_PHASE_AWAIT_CONFIG: return "CFG";
    case S1410_PHASE_READ_DATA:    return "READ";
    case S1410_PHASE_WRITE_DATA:   return "WRITE";
    case S1410_PHASE_RESPONSE:     return "RESP";
    default:                       return "?";
    }
}

} // namespace

void panels::render_xebec(partner &emu, bool *p_open)
{
    if (!ImGui::Begin("Xebec S1410", p_open,
                      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::End();
        return;
    }

    const s1410_t &hdc = emu.get_hdc();
    const idpartner_sasi_t &sasi = emu.get_sasi();
    const uint8_t status = idpartner_sasi_status_r(const_cast<idpartner_sasi_t*>(&sasi));

    ImGui::Text("Phase: %s", phase_name(hdc.phase));
    ImGui::Text("Present:%d Busy:%d Sess:%d DRQ:%d",
                hdc.present ? 1 : 0,
                hdc.busy ? 1 : 0,
                sasi.session_active ? 1 : 0,
                sasi.drq ? 1 : 0);
    ImGui::Separator();
    ImGui::Text("Status: %02X  REQ=%d IO=%d MSG=%d CD=%d BSY=%d",
                status,
                (status & 0x80) ? 1 : 0,
                (status & 0x40) ? 1 : 0,
                (status & 0x20) ? 1 : 0,
                (status & 0x10) ? 1 : 0,
                (status & 0x08) ? 1 : 0);
    ImGui::Text("Ctrl: %02X  data_en=%d drq_en=%d",
                sasi.last_ctrl,
                sasi.data_enable ? 1 : 0,
                sasi.drq_enable ? 1 : 0);
    ImGui::Text("Error: %02X", hdc.error);
    ImGui::Separator();
    ImGui::Text("Cfg: kind=%u len=%u exp=%u",
                (unsigned)hdc.cfg_kind,
                (unsigned)hdc.cfg_len,
                (unsigned)hdc.cfg_expected);
    ImGui::Text("Resp: len=%u idx=%u",
                (unsigned)hdc.response_len,
                (unsigned)hdc.response_idx);
    ImGui::Text("Data: len=%u idx=%u",
                (unsigned)hdc.data_len,
                (unsigned)hdc.data_idx);

    ImGui::End();
}
