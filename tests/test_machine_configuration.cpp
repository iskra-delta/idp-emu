#include "machine_configuration.hpp"

#include <array>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {

int fail(const char *message)
{
    std::fprintf(stderr, "FAIL %s\n", message);
    return 1;
}

bool valid_cmos_checksum(const std::array<uint8_t, 8> &cmos)
{
    uint8_t sum = 0;
    for (const uint8_t value : cmos) {
        sum = static_cast<uint8_t>((sum + (value & 0x0F)) & 0x0F);
        sum = static_cast<uint8_t>((sum + ((value >> 4) & 0x0F)) & 0x0F);
    }
    return sum == 0;
}

std::array<uint8_t, 8> read_cmos(const std::filesystem::path &path)
{
    std::array<uint8_t, 8> result{};
    std::ifstream input(path, std::ios::binary);
    input.read(reinterpret_cast<char *>(result.data()), result.size());
    return result;
}

} // namespace

int main()
{
    namespace fs = std::filesystem;
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path scratch = fs::path(IDP_SOURCE_ROOT) / "tests/dump" /
        ("machine-configuration-" + std::to_string(unique));
    fs::create_directories(scratch);

    machine_configuration cfg = default_machine_configuration();
    if (cfg.model != machine_model::crt ||
        cfg.sio[1].kind != partner::sio_device_kind::internal_squid)
        return fail("default configuration does not match current Partner P setup");

    cfg.model = machine_model::gdp;
    cfg.rom = "custom.rom";
    cfg.terminal = terminal_profile::vt100_ansi;
    cfg.boot = machine_boot_target::floppy;
    cfg.floppies[0] = {"fd0.img", floppy_media_type::dos_720};
    cfg.floppies[1] = {"fd1.img", floppy_media_type::dos_360};
    cfg.hard_disk = {"disk.img", hard_disk_type::st225};
    cfg.cmos_file = "machine.cmos";
    cfg.monitor.type = monitor_type::bw_crt;
    cfg.monitor.brightness = 1.35f;
    cfg.sio[1].kind = partner::sio_device_kind::none;
    cfg.sio[2].kind = partner::sio_device_kind::tcp_bridge;
    cfg.sio[2].tcp_data_port = 7611;
    cfg.sio[2].tcp_control_port = 7612;
    cfg.sio[2].tcp_require_rts = false;
    cfg.pio[0].kind = partner::pio_device_kind::covox;
    cfg.squid_payload_bytes = 64;
    cfg.partner_cpm_cmos.configured = true;
    cfg.partner_cpm_cmos.year = 26;
    cfg.partner_cpm_cmos.terminal = partner_terminal_type::partner;
    cfg.partner_cpm_cmos.language = partner_language::german;
    cfg.partner_cpm_cmos.screen_columns = 80;
    cfg.partner_cpm_cmos.reverse_video = true;
    cfg.partner_cpm_cmos.line_wrap = true;
    cfg.partner_cpm_cmos.auto_newline = true;
    cfg.partner_cpm_cmos.keyboard_layout = partner_keyboard_layout::qwertz;
    cfg.partner_cpm_cmos.key_click = true;
    cfg.partner_cpm_cmos.autorepeat = false;

    std::string error;
    const fs::path json_path = scratch / "configuration.json";
    if (!save_machine_configuration(json_path, cfg, error))
        return fail(error.c_str());
    machine_configuration roundtrip = default_machine_configuration();
    if (!load_machine_configuration(json_path, roundtrip, error))
        return fail(error.c_str());
    if (roundtrip.model != cfg.model || roundtrip.rom != cfg.rom ||
        roundtrip.terminal != terminal_profile::vt52 ||
        roundtrip.floppies[1].type != cfg.floppies[1].type ||
        roundtrip.hard_disk.type != cfg.hard_disk.type ||
        roundtrip.monitor.type != cfg.monitor.type ||
        roundtrip.sio[2].tcp_data_port != 7611 ||
        roundtrip.sio[2].tcp_require_rts ||
        roundtrip.pio[0].kind != partner::pio_device_kind::covox ||
        roundtrip.squid_payload_bytes != 64 ||
        !roundtrip.partner_cpm_cmos.configured ||
        roundtrip.partner_cpm_cmos.year != 26 ||
        roundtrip.partner_cpm_cmos.terminal != partner_terminal_type::partner ||
        roundtrip.partner_cpm_cmos.language != partner_language::german ||
        roundtrip.partner_cpm_cmos.screen_columns != 80 ||
        !roundtrip.partner_cpm_cmos.reverse_video ||
        !roundtrip.partner_cpm_cmos.line_wrap ||
        !roundtrip.partner_cpm_cmos.auto_newline ||
        roundtrip.partner_cpm_cmos.keyboard_layout != partner_keyboard_layout::qwertz ||
        !roundtrip.partner_cpm_cmos.key_click ||
        roundtrip.partner_cpm_cmos.autorepeat)
        return fail("JSON round trip changed configuration fields");

    const fs::path partial_path = scratch / "partial.json";
    {
        std::ofstream output(partial_path);
        output << R"({"monitor":{"type":"lcd"}})";
    }
    machine_configuration partial = cfg;
    if (!load_machine_configuration(partial_path, partial, error))
        return fail(error.c_str());
    if (partial.monitor.type != monitor_type::lcd ||
        partial.model != cfg.model || partial.rom != cfg.rom ||
        partial.sio[2].tcp_data_port != cfg.sio[2].tcp_data_port)
        return fail("missing JSON fields did not retain current values");

    machine_configuration invalid = default_machine_configuration();
    invalid.sio[2].kind = partner::sio_device_kind::internal_squid;
    if (validate_machine_configuration(invalid, error))
        return fail("duplicate internal Squid configuration was accepted");

    machine_configuration cmos_cfg = default_machine_configuration();
    cmos_cfg.floppies[0] = {"fd0.img", floppy_media_type::partner};
    cmos_cfg.floppies[1] = {"fd1.img", floppy_media_type::dos_720};
    cmos_cfg.hard_disk = {"hdd.img", hard_disk_type::st225};
    cmos_cfg.sio[1].kind = partner::sio_device_kind::internal_squid;
    cmos_cfg.sio[2].kind = partner::sio_device_kind::mouse_logitech;
    cmos_cfg.sio[3].kind = partner::sio_device_kind::none;
    cmos_cfg.pio[0].kind = partner::pio_device_kind::covox;
    cmos_cfg.pio[1].kind = partner::pio_device_kind::centronics_printer;
    const fs::path cmos_path = scratch / "partos.cmos";
    if (!prepare_machine_cmos(cmos_cfg, cmos_path, true, error))
        return fail(error.c_str());
    const auto cmos = read_cmos(cmos_path);
    if (cmos[1] != 0x60 || cmos[2] != 0xC0 ||
        cmos[3] != 0x1B || cmos[4] != 0x90 || !valid_cmos_checksum(cmos))
        return fail("PartOS CMOS hardware fields are incorrect");

    std::array<uint8_t, 8> legacy_seed{{0x00, 0x00, 0x80, 0x08, 0x40, 0x00, 0x90, 0x4F}};
    const fs::path legacy_path = scratch / "legacy.cmos";
    {
        std::ofstream output(legacy_path, std::ios::binary);
        output.write(reinterpret_cast<const char *>(legacy_seed.data()), legacy_seed.size());
    }

    machine_configuration unconfigured_legacy = default_machine_configuration();
    if (!prepare_machine_cmos(unconfigured_legacy, legacy_path, false, error))
        return fail(error.c_str());
    if (read_cmos(legacy_path) != legacy_seed)
        return fail("unconfigured legacy preparation did not preserve CMOS");

    partner_cpm_cmos_configuration decoded;
    if (!load_partner_cpm_cmos(legacy_path, machine_model::gdp, decoded, error))
        return fail(error.c_str());
    if (!decoded.configured || decoded.year != 0 ||
        decoded.terminal != partner_terminal_type::ansi ||
        decoded.language != partner_language::yugoslav ||
        decoded.screen_columns != 132 || decoded.reverse_video || decoded.line_wrap ||
        decoded.auto_newline || decoded.keyboard_layout != partner_keyboard_layout::qwerty ||
        decoded.key_click || !decoded.autorepeat)
        return fail("legacy Partner CMOS settings were decoded incorrectly");

    cmos_cfg.partner_cpm_cmos.configured = true;
    cmos_cfg.partner_cpm_cmos.year = 26;
    cmos_cfg.partner_cpm_cmos.terminal = partner_terminal_type::vt52;
    cmos_cfg.partner_cpm_cmos.language = partner_language::german;
    cmos_cfg.partner_cpm_cmos.screen_columns = 80;
    cmos_cfg.partner_cpm_cmos.reverse_video = true;
    cmos_cfg.partner_cpm_cmos.line_wrap = false;
    cmos_cfg.partner_cpm_cmos.auto_newline = true;
    cmos_cfg.partner_cpm_cmos.keyboard_layout = partner_keyboard_layout::qwertz;
    cmos_cfg.partner_cpm_cmos.key_click = true;
    cmos_cfg.partner_cpm_cmos.autorepeat = false;
    if (!prepare_machine_cmos(cmos_cfg, legacy_path, false, error))
        return fail(error.c_str());
    const auto legacy = read_cmos(legacy_path);
    if (legacy[1] != 0x26 || legacy[2] != legacy_seed[2] ||
        legacy[3] != 0x24 || legacy[4] != 0x51 ||
        legacy[5] != legacy_seed[5] || legacy[6] != 0x95 ||
        legacy[7] != 0xE7 || legacy[0] != legacy_seed[0])
        return fail("legacy Partner CMOS preferences are incorrect");

    const auto legacy_before_partos = legacy;
    if (!prepare_machine_cmos(cmos_cfg, legacy_path, true, error))
        return fail(error.c_str());
    const auto partos_overwrite = read_cmos(legacy_path);
    if (partos_overwrite[3] == 0x24 || partos_overwrite[4] == 0x51 ||
        partos_overwrite == legacy_before_partos)
        return fail("PartOS incorrectly used the legacy CP/M preference layout");

    std::error_code cleanup_error;
    fs::remove_all(scratch, cleanup_error);
    return 0;
}
