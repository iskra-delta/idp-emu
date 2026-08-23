#include "panel_devices.hpp"
#include "../partner.hpp"
#include <imgui.h>
#include <array>
#include <string>

namespace {

const char *sio_port_label(partner::sio_port_id port)
{
    switch (port)
    {
    case partner::sio_port_id::sio1_a: return "SIO1 Port A";
    case partner::sio_port_id::sio1_b: return "SIO1 Port B (PAKET 2)";
    case partner::sio_port_id::sio2_a: return "SIO2 Port A (PAKET 3)";
    case partner::sio_port_id::sio2_b: return "SIO2 Port B (PAKET 4)";
    }
    return "SIO ?";
}

const char *sio_kind_label(partner::sio_device_kind kind)
{
    switch (kind)
    {
    case partner::sio_device_kind::none: return "None";
    case partner::sio_device_kind::mouse_microsoft: return "Serial Mouse (Microsoft)";
    case partner::sio_device_kind::mouse_mousesystems: return "Serial Mouse (Mouse Systems)";
    case partner::sio_device_kind::mouse_logitech: return "Serial Mouse (Logitech)";
    case partner::sio_device_kind::tcp_bridge: return "TCP Bridge";
    case partner::sio_device_kind::internal_squid: return "Internal Squid (Retro Vault)";
    }
    return "Unknown";
}

const char *pio_port_label(partner::pio_port_id port)
{
    return (port == partner::pio_port_id::a) ? "PIO Port A" : "PIO Port B";
}

const char *pio_kind_label(partner::pio_device_kind kind)
{
    switch (kind)
    {
    case partner::pio_device_kind::none: return "None";
    case partner::pio_device_kind::covox: return "Covox DAC";
    case partner::pio_device_kind::centronics_printer: return "Centronics Printer (Visual)";
    }
    return "Unknown";
}

bool has_mouse_device(partner &emu)
{
    const std::array<partner::sio_port_id, 3> ports = {
        partner::sio_port_id::sio1_b,
        partner::sio_port_id::sio2_a,
        partner::sio_port_id::sio2_b
    };
    for (const auto port : ports)
    {
        const auto cfg = emu.get_sio_device_config(port);
        if (cfg.kind == partner::sio_device_kind::mouse_microsoft ||
            cfg.kind == partner::sio_device_kind::mouse_mousesystems ||
            cfg.kind == partner::sio_device_kind::mouse_logitech)
        {
            return true;
        }
    }
    return false;
}

void render_sio_port(partner &emu, partner::sio_port_id port)
{
    auto cfg = emu.get_sio_device_config(port);
    const auto st = emu.get_sio_port_status(port);
    const bool locked = emu.is_sio_port_locked(port);

    ImGui::PushID((int)port);
    ImGui::SeparatorText(sio_port_label(port));

    if (locked)
    {
        ImGui::TextUnformatted("Attachment: fixed/internal");
        const std::string why = emu.get_sio_port_lock_reason(port);
        if (!why.empty())
            ImGui::Text("Reason: %s", why.c_str());
    }
    else
    {
        const std::array<partner::sio_device_kind, 6> kinds = {
            partner::sio_device_kind::none,
            partner::sio_device_kind::mouse_microsoft,
            partner::sio_device_kind::mouse_mousesystems,
            partner::sio_device_kind::mouse_logitech,
            partner::sio_device_kind::tcp_bridge,
            partner::sio_device_kind::internal_squid
        };
        int current_idx = 0;
        for (int i = 0; i < (int)kinds.size(); i++) {
            if (kinds[i] == cfg.kind) {
                current_idx = i;
                break;
            }
        }

        if (ImGui::BeginCombo("Attach", sio_kind_label(cfg.kind)))
        {
            for (int i = 0; i < (int)kinds.size(); i++)
            {
                const bool selected = (i == current_idx);
                if (ImGui::Selectable(sio_kind_label(kinds[i]), selected))
                {
                    cfg.kind = kinds[i];
                    if (cfg.kind == partner::sio_device_kind::tcp_bridge)
                    {
                        if (cfg.tcp_data_port <= 0)
                            cfg.tcp_data_port = 6601 + ((int)port * 10);
                        if (cfg.tcp_control_port <= 0)
                            cfg.tcp_control_port = cfg.tcp_data_port + 1;
                    }
                    (void)emu.set_sio_device_config(port, cfg);
                }
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        if (cfg.kind == partner::sio_device_kind::tcp_bridge)
        {
            int data_port = cfg.tcp_data_port;
            int ctrl_port = cfg.tcp_control_port;
            bool req_rts = cfg.tcp_require_rts;
            bool cts_auto = cfg.tcp_cts_follows_data_client;
            bool changed = false;

            if (ImGui::InputInt("Data TCP Port", &data_port, 1, 100))
                changed = true;
            if (ImGui::InputInt("Control TCP Port", &ctrl_port, 1, 100))
                changed = true;
            if (ImGui::Checkbox("Require RTS for incoming data", &req_rts))
                changed = true;
            if (ImGui::Checkbox("CTS follows data connection", &cts_auto))
                changed = true;
            ImGui::TextUnformatted("Control socket accepts: CTS 0|1|AUTO, DCD 0|1|AUTO.");

            if (changed)
            {
                cfg.tcp_data_port = data_port;
                cfg.tcp_control_port = ctrl_port;
                cfg.tcp_require_rts = req_rts;
                cfg.tcp_cts_follows_data_client = cts_auto;
                (void)emu.set_sio_device_config(port, cfg);
            }
        }
    }

    ImGui::Text("Status: %s", st.detail.empty() ? "" : st.detail.c_str());
    ImGui::Text("Line state: CTS=%d DCD=%d RTS=%d DTR=%d", st.cts ? 1 : 0, st.dcd ? 1 : 0, st.rts ? 1 : 0, st.dtr ? 1 : 0);
    ImGui::Text("Traffic: RX queued=%zu  RX bytes=%llu  TX bytes=%llu",
                st.pending_rx_bytes,
                (unsigned long long)st.rx_bytes,
                (unsigned long long)st.tx_bytes);
    ImGui::PopID();
}

void render_pio_port(partner &emu, partner::pio_port_id port)
{
    auto cfg = emu.get_pio_device_config(port);
    const auto st = emu.get_pio_port_status(port);

    ImGui::PushID((int)port + 100);
    ImGui::SeparatorText(pio_port_label(port));

    const std::array<partner::pio_device_kind, 3> kinds = {
        partner::pio_device_kind::none,
        partner::pio_device_kind::covox,
        partner::pio_device_kind::centronics_printer
    };

    if (ImGui::BeginCombo("Attach", pio_kind_label(cfg.kind)))
    {
        for (int i = 0; i < (int)kinds.size(); i++)
        {
            const bool selected = (kinds[i] == cfg.kind);
            if (ImGui::Selectable(pio_kind_label(kinds[i]), selected))
            {
                cfg.kind = kinds[i];
                emu.set_pio_device_config(port, cfg);
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::Text("Last output: 0x%02X", st.last_output);
    ImGui::Text("Bytes seen: %llu", (unsigned long long)st.bytes_seen);
    if (cfg.kind == partner::pio_device_kind::covox)
    {
        ImGui::ProgressBar(st.covox_level, ImVec2(-1.0f, 0.0f), "Covox level");
    }

    ImGui::PopID();
}

} // namespace

void panels::render_devices(partner &emu, bool *p_open)
{
    if (!ImGui::Begin("Devices", p_open,
                      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::End();
        return;
    }

    ImGui::SeparatorText("Serial Devices (SIO)");
    render_sio_port(emu, partner::sio_port_id::sio1_a);
    render_sio_port(emu, partner::sio_port_id::sio1_b);
    render_sio_port(emu, partner::sio_port_id::sio2_a);
    render_sio_port(emu, partner::sio_port_id::sio2_b);

    ImGui::SeparatorText("PIO Devices");
    render_pio_port(emu, partner::pio_port_id::a);
    render_pio_port(emu, partner::pio_port_id::b);

    if (has_mouse_device(emu))
    {
        ImGui::SeparatorText("Virtual Mouse Feed");
        static bool btn_left = false;
        static bool btn_right = false;
        static bool btn_middle = false;
        static int step = 3;

        ImGui::TextUnformatted("Mouse: click in Partner Display to lock; Left Ctrl or Right Ctrl releases.");
        ImGui::Checkbox("Left", &btn_left);
        ImGui::SameLine();
        ImGui::Checkbox("Right", &btn_right);
        ImGui::SameLine();
        ImGui::Checkbox("Middle", &btn_middle);
        ImGui::InputInt("Step", &step, 1, 10);
        if (step < 1) step = 1;

        if (ImGui::Button("Left##move")) emu.inject_serial_mouse_motion(-step, 0, btn_left, btn_right, btn_middle);
        ImGui::SameLine();
        if (ImGui::Button("Right##move")) emu.inject_serial_mouse_motion(step, 0, btn_left, btn_right, btn_middle);
        ImGui::SameLine();
        if (ImGui::Button("Up##move")) emu.inject_serial_mouse_motion(0, -step, btn_left, btn_right, btn_middle);
        ImGui::SameLine();
        if (ImGui::Button("Down##move")) emu.inject_serial_mouse_motion(0, step, btn_left, btn_right, btn_middle);
    }

    const bool printer_on_a = emu.get_pio_device_config(partner::pio_port_id::a).kind == partner::pio_device_kind::centronics_printer;
    const bool printer_on_b = emu.get_pio_device_config(partner::pio_port_id::b).kind == partner::pio_device_kind::centronics_printer;
    if (printer_on_a || printer_on_b)
    {
        ImGui::SeparatorText("Visual Printer");
        if (ImGui::Button("Clear Printer Output"))
            emu.clear_virtual_printer_text();
        ImGui::BeginChild("printer_output", ImVec2(0.0f, 180.0f), true, ImGuiWindowFlags_HorizontalScrollbar);
        const std::string &printer_text = emu.get_virtual_printer_text();
        if (printer_text.empty())
            ImGui::TextUnformatted("(no output yet)");
        else
            ImGui::TextUnformatted(printer_text.c_str());
        ImGui::EndChild();
    }

    ImGui::End();
}
