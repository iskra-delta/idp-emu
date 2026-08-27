#include "machine_configuration.hpp"
#include "squid_link_server.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <map>
#include <system_error>

using nlohmann::json;

namespace {

template<typename T>
bool read_enum(const json &value, const std::map<std::string, T> &values,
               T &destination, const char *field, std::string &error)
{
    if (!value.is_string()) {
        error = std::string(field) + " must be a string";
        return false;
    }
    const auto it = values.find(value.get<std::string>());
    if (it == values.end()) {
        error = std::string("unknown ") + field + ": " + value.get<std::string>();
        return false;
    }
    destination = it->second;
    return true;
}

template<typename T>
std::string enum_name(T value, const std::map<std::string, T> &values)
{
    for (const auto &[name, candidate] : values) {
        if (candidate == value)
            return name;
    }
    return values.begin()->first;
}

const std::map<std::string, machine_model> model_names{
    {"crt", machine_model::crt}, {"gdp", machine_model::gdp}
};
const std::map<std::string, machine_boot_target> boot_names{
    {"default", machine_boot_target::default_target},
    {"floppy", machine_boot_target::floppy}
};
const std::map<std::string, floppy_media_type> floppy_names{
    {"none", floppy_media_type::free},
    {"partner", floppy_media_type::partner},
    {"dos720", floppy_media_type::dos_720},
    {"dos360", floppy_media_type::dos_360}
};
const std::map<std::string, hard_disk_type> hard_disk_names{
    {"none", hard_disk_type::free}, {"st506", hard_disk_type::st506},
    {"st412", hard_disk_type::st412}, {"st225", hard_disk_type::st225}
};
const std::map<std::string, monitor_type> monitor_names{
    {"flat", monitor_type::flat}, {"green_crt", monitor_type::green_crt},
    {"orange_crt", monitor_type::orange_crt}, {"bw_crt", monitor_type::bw_crt},
    {"lcd", monitor_type::lcd}
};
const std::map<std::string, partner_terminal_type> partner_terminal_names{
    {"ansi", partner_terminal_type::ansi},
    {"partner", partner_terminal_type::partner},
    {"vt52", partner_terminal_type::vt52}
};
const std::map<std::string, partner_language> language_names{
    {"us_ascii", partner_language::us_ascii},
    {"uk_ascii", partner_language::uk_ascii},
    {"spanish", partner_language::spanish},
    {"french", partner_language::french},
    {"german", partner_language::german},
    {"italian", partner_language::italian},
    {"danish", partner_language::danish},
    {"swedish", partner_language::swedish},
    {"yugoslav", partner_language::yugoslav}
};
const std::map<std::string, partner_keyboard_layout> keyboard_layout_names{
    {"qwerty", partner_keyboard_layout::qwerty},
    {"qwertz", partner_keyboard_layout::qwertz}
};
const std::map<std::string, partner::sio_device_kind> sio_names{
    {"none", partner::sio_device_kind::none},
    {"mouse_microsoft", partner::sio_device_kind::mouse_microsoft},
    {"mouse_mousesystems", partner::sio_device_kind::mouse_mousesystems},
    {"mouse_logitech", partner::sio_device_kind::mouse_logitech},
    {"tcp_bridge", partner::sio_device_kind::tcp_bridge},
    {"internal_squid", partner::sio_device_kind::internal_squid}
};
const std::map<std::string, partner::pio_device_kind> pio_names{
    {"none", partner::pio_device_kind::none},
    {"covox", partner::pio_device_kind::covox},
    {"centronics_printer", partner::pio_device_kind::centronics_printer}
};

bool read_string(const json &object, const char *key, std::string &value,
                 const char *field, std::string &error)
{
    const auto it = object.find(key);
    if (it == object.end())
        return true;
    if (!it->is_string()) {
        error = std::string(field) + " must be a string";
        return false;
    }
    value = it->get<std::string>();
    return true;
}

bool read_float(const json &object, const char *key, float &value,
                const char *field, std::string &error)
{
    const auto it = object.find(key);
    if (it == object.end())
        return true;
    if (!it->is_number()) {
        error = std::string(field) + " must be a number";
        return false;
    }
    value = it->get<float>();
    return true;
}

bool read_bool(const json &object, const char *key, bool &value,
               const char *field, std::string &error)
{
    const auto it = object.find(key);
    if (it == object.end())
        return true;
    if (!it->is_boolean()) {
        error = std::string(field) + " must be boolean";
        return false;
    }
    value = it->get<bool>();
    return true;
}

bool read_sio(const json &value, partner::sio_device_config &cfg,
              const char *field, std::string &error)
{
    if (!value.is_object()) {
        error = std::string(field) + " must be an object";
        return false;
    }
    if (const auto it = value.find("device"); it != value.end() &&
        !read_enum(*it, sio_names, cfg.kind, "SIO device", error))
        return false;
    if (const auto it = value.find("data_port"); it != value.end()) {
        if (!it->is_number_integer()) { error = "SIO data_port must be an integer"; return false; }
        cfg.tcp_data_port = it->get<int>();
    }
    if (const auto it = value.find("control_port"); it != value.end()) {
        if (!it->is_number_integer()) { error = "SIO control_port must be an integer"; return false; }
        cfg.tcp_control_port = it->get<int>();
    }
    if (const auto it = value.find("require_rts"); it != value.end()) {
        if (!it->is_boolean()) { error = "SIO require_rts must be boolean"; return false; }
        cfg.tcp_require_rts = it->get<bool>();
    }
    if (const auto it = value.find("cts_follows_data_client"); it != value.end()) {
        if (!it->is_boolean()) { error = "SIO cts_follows_data_client must be boolean"; return false; }
        cfg.tcp_cts_follows_data_client = it->get<bool>();
    }
    return true;
}

json write_sio(const partner::sio_device_config &cfg)
{
    return json{{"device", enum_name(cfg.kind, sio_names)},
                {"data_port", cfg.tcp_data_port},
                {"control_port", cfg.tcp_control_port},
                {"require_rts", cfg.tcp_require_rts},
                {"cts_follows_data_client", cfg.tcp_cts_follows_data_client}};
}

uint8_t partos_sio_value(partner::sio_device_kind kind)
{
    switch (kind) {
    case partner::sio_device_kind::mouse_microsoft:
    case partner::sio_device_kind::mouse_mousesystems:
    case partner::sio_device_kind::mouse_logitech:
        return 2;
    case partner::sio_device_kind::tcp_bridge:
    case partner::sio_device_kind::internal_squid:
        return 1;
    case partner::sio_device_kind::none:
        return 3;
    }
    return 3;
}

uint8_t partos_pio_value(partner::pio_device_kind kind)
{
    switch (kind) {
    case partner::pio_device_kind::none: return 0;
    case partner::pio_device_kind::centronics_printer: return 1;
    case partner::pio_device_kind::covox: return 2;
    }
    return 0;
}

void stamp_checksum(std::array<uint8_t, 8> &cmos)
{
    cmos[7] &= 0xF0;
    uint8_t sum = 0;
    for (const uint8_t value : cmos) {
        sum = static_cast<uint8_t>((sum + (value & 0x0F)) & 0x0F);
        sum = static_cast<uint8_t>((sum + ((value >> 4) & 0x0F)) & 0x0F);
    }
    cmos[7] |= static_cast<uint8_t>((-sum) & 0x0F);
}

bool read_cmos_image(const std::filesystem::path &path,
                     std::array<uint8_t, 8> &cmos, std::string &error)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "cannot open CMOS file: " + path.string();
        return false;
    }
    input.read(reinterpret_cast<char *>(cmos.data()), cmos.size());
    if (input.gcount() != static_cast<std::streamsize>(cmos.size()) ||
        input.peek() != std::ifstream::traits_type::eof()) {
        error = "CMOS file must contain exactly eight bytes: " + path.string();
        return false;
    }
    return true;
}

} // namespace

machine_configuration default_machine_configuration()
{
    machine_configuration cfg;
    cfg.sio[1].kind = partner::sio_device_kind::internal_squid;
    return cfg;
}

bool validate_machine_configuration(const machine_configuration &cfg,
                                    std::string &error)
{
    if (cfg.version != 1) {
        error = "unsupported configuration version: " + std::to_string(cfg.version);
        return false;
    }
    if (cfg.rom.empty()) {
        error = "rom must not be empty";
        return false;
    }
    if (cfg.cmos_file.empty()) {
        error = "cmos.file must not be empty";
        return false;
    }
    for (const auto &floppy : cfg.floppies) {
        if (!floppy.image.empty() && floppy.type == floppy_media_type::free) {
            error = "a floppy with an image must have a non-none media type";
            return false;
        }
    }
    if (!cfg.hard_disk.image.empty() && cfg.hard_disk.type == hard_disk_type::free) {
        error = "a hard disk with an image must have a non-none type";
        return false;
    }
    unsigned squid_count = 0;
    for (std::size_t i = 1; i < cfg.sio.size(); ++i) {
        const auto &sio = cfg.sio[i];
        if (sio.kind == partner::sio_device_kind::internal_squid)
            ++squid_count;
        if (sio.kind == partner::sio_device_kind::tcp_bridge &&
            (sio.tcp_data_port < 1 || sio.tcp_data_port > 65535 ||
             sio.tcp_control_port < 1 || sio.tcp_control_port > 65535 ||
             sio.tcp_data_port == sio.tcp_control_port)) {
            error = "SIO TCP ports must be distinct values from 1 to 65535";
            return false;
        }
    }
    if (squid_count > 1) {
        error = "internal_squid may be attached to only one SIO port";
        return false;
    }
    if (cfg.squid_payload_bytes < squid_link_server::minimum_payload_bytes ||
        cfg.squid_payload_bytes > squid_link_server::maximum_payload_bytes) {
        error = "squid_payload_bytes must be from 16 to 112";
        return false;
    }
    if (cfg.partner_cpm_cmos.configured) {
        if (cfg.partner_cpm_cmos.year > 99) {
            error = "partner_cpm_cmos.year must be from 0 to 99";
            return false;
        }
        if (cfg.partner_cpm_cmos.screen_columns != 80 &&
            cfg.partner_cpm_cmos.screen_columns != 132) {
            error = "partner_cpm_cmos.screen_columns must be 80 or 132";
            return false;
        }
    }
    const auto valid_tuning = [](float value) { return std::isfinite(value); };
    if (!valid_tuning(cfg.monitor.brightness) || !valid_tuning(cfg.monitor.contrast) ||
        !valid_tuning(cfg.monitor.bloom) || !valid_tuning(cfg.monitor.scanline_strength) ||
        !valid_tuning(cfg.monitor.mask_strength) || !valid_tuning(cfg.monitor.vignette) ||
        !valid_tuning(cfg.monitor.persistence)) {
        error = "monitor tuning values must be finite numbers";
        return false;
    }
    error.clear();
    return true;
}

bool load_machine_configuration(const std::filesystem::path &path,
                                machine_configuration &cfg,
                                std::string &error)
{
    try {
        std::ifstream stream(path);
        if (!stream) {
            error = "cannot open configuration: " + path.string();
            return false;
        }
        json root;
        stream >> root;
        if (!root.is_object()) { error = "configuration root must be an object"; return false; }

        if (const auto it = root.find("version"); it != root.end()) {
            if (!it->is_number_unsigned() && !it->is_number_integer()) {
                error = "version must be an integer"; return false;
            }
            cfg.version = it->get<unsigned>();
        }
        if (const auto it = root.find("model"); it != root.end() &&
            !read_enum(*it, model_names, cfg.model, "model", error)) return false;
        if (!read_string(root, "rom", cfg.rom, "rom", error)) return false;
        if (const auto it = root.find("terminal"); it != root.end()) {
            const std::map<std::string, terminal_profile> names{
                {"vt52", terminal_profile::vt52}, {"ansi", terminal_profile::vt100_ansi},
                {"vt100", terminal_profile::vt100_ansi}};
            if (!read_enum(*it, names, cfg.terminal, "terminal", error)) return false;
        }
        if (const auto it = root.find("boot"); it != root.end() &&
            !read_enum(*it, boot_names, cfg.boot, "boot", error)) return false;

        if (const auto storage = root.find("storage"); storage != root.end()) {
            if (!storage->is_object()) { error = "storage must be an object"; return false; }
            if (const auto floppies = storage->find("floppies"); floppies != storage->end()) {
                if (!floppies->is_array() || floppies->size() > 2) {
                    error = "storage.floppies must be an array of at most two entries"; return false;
                }
                for (std::size_t i = 0; i < floppies->size(); ++i) {
                    const auto &item = (*floppies)[i];
                    if (!item.is_object()) { error = "floppy entry must be an object"; return false; }
                    if (!read_string(item, "image", cfg.floppies[i].image,
                                     "floppy image", error)) return false;
                    if (const auto type = item.find("type"); type != item.end() &&
                        !read_enum(*type, floppy_names, cfg.floppies[i].type,
                                   "floppy type", error)) return false;
                }
            }
            if (const auto hdd = storage->find("hard_disk"); hdd != storage->end()) {
                if (!hdd->is_object()) { error = "storage.hard_disk must be an object"; return false; }
                if (!read_string(*hdd, "image", cfg.hard_disk.image,
                                 "hard disk image", error)) return false;
                if (const auto type = hdd->find("type"); type != hdd->end() &&
                    !read_enum(*type, hard_disk_names, cfg.hard_disk.type,
                               "hard disk type", error)) return false;
            }
        }

        if (const auto cmos = root.find("cmos"); cmos != root.end()) {
            if (!cmos->is_object()) { error = "cmos must be an object"; return false; }
            if (!read_string(*cmos, "file", cfg.cmos_file, "cmos.file", error)) return false;
            // Accepted but deliberately ignored for compatibility with JSON
            // files written by the initial configuration implementation.
            if (const auto prepare = cmos->find("prepare_on_restart"); prepare != cmos->end()) {
                if (!prepare->is_boolean()) { error = "cmos.prepare_on_restart must be boolean"; return false; }
            }
        }

        if (const auto partner_cmos = root.find("partner_cpm_cmos");
            partner_cmos != root.end()) {
            if (!partner_cmos->is_object()) {
                error = "partner_cpm_cmos must be an object";
                return false;
            }
            auto &settings = cfg.partner_cpm_cmos;
            settings.configured = true;
            if (const auto year = partner_cmos->find("year"); year != partner_cmos->end()) {
                if (!year->is_number_integer()) {
                    error = "partner_cpm_cmos.year must be an integer";
                    return false;
                }
                const int value = year->get<int>();
                if (value < 0 || value > 99) {
                    error = "partner_cpm_cmos.year must be from 0 to 99";
                    return false;
                }
                settings.year = static_cast<uint8_t>(value);
            }
            if (const auto terminal = partner_cmos->find("terminal");
                terminal != partner_cmos->end() &&
                !read_enum(*terminal, partner_terminal_names, settings.terminal,
                           "partner_cpm_cmos.terminal", error)) return false;
            if (const auto language = partner_cmos->find("language");
                language != partner_cmos->end() &&
                !read_enum(*language, language_names, settings.language,
                           "partner_cpm_cmos.language", error)) return false;
            if (const auto columns = partner_cmos->find("screen_columns");
                columns != partner_cmos->end()) {
                if (!columns->is_number_integer()) {
                    error = "partner_cpm_cmos.screen_columns must be an integer";
                    return false;
                }
                const int value = columns->get<int>();
                if (value != 80 && value != 132) {
                    error = "partner_cpm_cmos.screen_columns must be 80 or 132";
                    return false;
                }
                settings.screen_columns = static_cast<uint16_t>(value);
            }
            if (!read_bool(*partner_cmos, "reverse_video", settings.reverse_video,
                           "partner_cpm_cmos.reverse_video", error) ||
                !read_bool(*partner_cmos, "line_wrap", settings.line_wrap,
                           "partner_cpm_cmos.line_wrap", error) ||
                !read_bool(*partner_cmos, "auto_newline", settings.auto_newline,
                           "partner_cpm_cmos.auto_newline", error) ||
                !read_bool(*partner_cmos, "key_click", settings.key_click,
                           "partner_cpm_cmos.key_click", error) ||
                !read_bool(*partner_cmos, "autorepeat", settings.autorepeat,
                           "partner_cpm_cmos.autorepeat", error)) return false;
            if (const auto layout = partner_cmos->find("keyboard_layout");
                layout != partner_cmos->end() &&
                !read_enum(*layout, keyboard_layout_names, settings.keyboard_layout,
                           "partner_cpm_cmos.keyboard_layout", error)) return false;
        }

        if (const auto monitor = root.find("monitor"); monitor != root.end()) {
            if (!monitor->is_object()) { error = "monitor must be an object"; return false; }
            if (const auto type = monitor->find("type"); type != monitor->end() &&
                !read_enum(*type, monitor_names, cfg.monitor.type,
                           "monitor type", error)) return false;
            if (!read_float(*monitor, "brightness", cfg.monitor.brightness, "monitor.brightness", error) ||
                !read_float(*monitor, "contrast", cfg.monitor.contrast, "monitor.contrast", error) ||
                !read_float(*monitor, "bloom", cfg.monitor.bloom, "monitor.bloom", error) ||
                !read_float(*monitor, "scanline_strength", cfg.monitor.scanline_strength, "monitor.scanline_strength", error) ||
                !read_float(*monitor, "mask_strength", cfg.monitor.mask_strength, "monitor.mask_strength", error) ||
                !read_float(*monitor, "vignette", cfg.monitor.vignette, "monitor.vignette", error) ||
                !read_float(*monitor, "persistence", cfg.monitor.persistence, "monitor.persistence", error)) return false;
        }

        if (const auto sio = root.find("sio"); sio != root.end()) {
            if (!sio->is_object()) { error = "sio must be an object"; return false; }
            for (std::size_t i = 1; i < cfg.sio.size(); ++i) {
                const std::string key = std::to_string(i + 1);
                if (const auto port = sio->find(key); port != sio->end() &&
                    !read_sio(*port, cfg.sio[i], ("sio." + key).c_str(), error)) return false;
            }
        }
        if (const auto pio = root.find("pio"); pio != root.end()) {
            if (!pio->is_object()) { error = "pio must be an object"; return false; }
            for (std::size_t i = 0; i < cfg.pio.size(); ++i) {
                const char *key = i == 0 ? "a" : "b";
                if (const auto port = pio->find(key); port != pio->end()) {
                    if (!port->is_object()) { error = std::string("pio.") + key + " must be an object"; return false; }
                    if (const auto device = port->find("device"); device != port->end() &&
                        !read_enum(*device, pio_names, cfg.pio[i].kind,
                                   "PIO device", error)) return false;
                }
            }
        }
        if (const auto payload = root.find("squid_payload_bytes"); payload != root.end()) {
            if (!payload->is_number_integer()) { error = "squid_payload_bytes must be an integer"; return false; }
            cfg.squid_payload_bytes = payload->get<uint32_t>();
        }
        return validate_machine_configuration(cfg, error);
    } catch (const std::exception &e) {
        error = std::string("cannot parse configuration: ") + e.what();
        return false;
    }
}

bool save_machine_configuration(const std::filesystem::path &path,
                                const machine_configuration &cfg,
                                std::string &error)
{
    if (!validate_machine_configuration(cfg, error))
        return false;
    try {
        json root{
            {"version", cfg.version},
            {"model", enum_name(cfg.model, model_names)},
            {"rom", cfg.rom},
            {"boot", enum_name(cfg.boot, boot_names)},
            {"storage", {
                {"floppies", json::array({
                    json{{"image", cfg.floppies[0].image}, {"type", enum_name(cfg.floppies[0].type, floppy_names)}},
                    json{{"image", cfg.floppies[1].image}, {"type", enum_name(cfg.floppies[1].type, floppy_names)}}})},
                {"hard_disk", {{"image", cfg.hard_disk.image},
                               {"type", enum_name(cfg.hard_disk.type, hard_disk_names)}}}
            }},
            {"cmos", {{"file", cfg.cmos_file}}},
            {"monitor", {
                {"type", enum_name(cfg.monitor.type, monitor_names)},
                {"brightness", cfg.monitor.brightness}, {"contrast", cfg.monitor.contrast},
                {"bloom", cfg.monitor.bloom}, {"scanline_strength", cfg.monitor.scanline_strength},
                {"mask_strength", cfg.monitor.mask_strength}, {"vignette", cfg.monitor.vignette},
                {"persistence", cfg.monitor.persistence}
            }},
            {"sio", {{"2", write_sio(cfg.sio[1])}, {"3", write_sio(cfg.sio[2])},
                     {"4", write_sio(cfg.sio[3])}}},
            {"pio", {{"a", {{"device", enum_name(cfg.pio[0].kind, pio_names)}}},
                     {"b", {{"device", enum_name(cfg.pio[1].kind, pio_names)}}}}},
            {"squid_payload_bytes", cfg.squid_payload_bytes}
        };
        if (cfg.model == machine_model::crt)
            root["terminal"] = cfg.terminal == terminal_profile::vt52 ? "vt52" : "ansi";
        if (cfg.partner_cpm_cmos.configured) {
            const auto &settings = cfg.partner_cpm_cmos;
            root["partner_cpm_cmos"] = {
                {"year", settings.year},
                {"terminal", enum_name(settings.terminal, partner_terminal_names)},
                {"language", enum_name(settings.language, language_names)},
                {"screen_columns", settings.screen_columns},
                {"reverse_video", settings.reverse_video},
                {"line_wrap", settings.line_wrap},
                {"auto_newline", settings.auto_newline},
                {"keyboard_layout", enum_name(settings.keyboard_layout,
                                               keyboard_layout_names)},
                {"key_click", settings.key_click},
                {"autorepeat", settings.autorepeat}
            };
        }

        if (!path.parent_path().empty())
            std::filesystem::create_directories(path.parent_path());
        const auto temporary = path.string() + ".tmp";
        {
            std::ofstream stream(temporary, std::ios::trunc);
            if (!stream) { error = "cannot write configuration: " + temporary; return false; }
            stream << root.dump(2) << '\n';
            if (!stream) { error = "cannot finish configuration: " + temporary; return false; }
        }
        std::error_code ec;
        std::filesystem::remove(path, ec);
        ec.clear();
        std::filesystem::rename(temporary, path, ec);
        if (ec) { error = "cannot replace configuration: " + ec.message(); return false; }
        error.clear();
        return true;
    } catch (const std::exception &e) {
        error = std::string("cannot save configuration: ") + e.what();
        return false;
    }
}

bool load_partner_cpm_cmos(const std::filesystem::path &path,
                           machine_model model,
                           partner_cpm_cmos_configuration &configuration,
                           std::string &error)
{
    try {
        std::array<uint8_t, 8> cmos{};
        if (!read_cmos_image(path, cmos, error))
            return false;

        const uint8_t year_tens = static_cast<uint8_t>(cmos[1] >> 4);
        const uint8_t year_ones = static_cast<uint8_t>(cmos[1] & 0x0F);
        configuration.year = year_tens <= 9 && year_ones <= 9
            ? static_cast<uint8_t>(year_tens * 10 + year_ones) : 0;

        const uint8_t terminal = static_cast<uint8_t>(cmos[3] >> 4);
        configuration.terminal = terminal <= 2
            ? static_cast<partner_terminal_type>(terminal)
            : (model == machine_model::gdp ? partner_terminal_type::ansi
                                           : partner_terminal_type::vt52);
        const uint8_t language = static_cast<uint8_t>(cmos[3] & 0x0F);
        configuration.language = language <= 8
            ? static_cast<partner_language>(language) : partner_language::yugoslav;
        configuration.screen_columns = cmos[4] == 0x51 ? 80 :
            (cmos[4] == 0x85 ? 132 : (model == machine_model::gdp ? 132 : 80));
        configuration.reverse_video = (cmos[6] & 0x01) != 0;
        configuration.line_wrap = (cmos[6] & 0x02) != 0;
        configuration.auto_newline = (cmos[6] & 0x04) != 0;
        configuration.keyboard_layout = (cmos[7] & 0x80) != 0
            ? partner_keyboard_layout::qwertz : partner_keyboard_layout::qwerty;
        configuration.key_click = (cmos[7] & 0x08) == 0;
        configuration.autorepeat = (cmos[7] & 0x20) == 0;
        configuration.configured = true;
        error.clear();
        return true;
    } catch (const std::exception &e) {
        error = std::string("cannot read Partner CP/M CMOS: ") + e.what();
        return false;
    }
}

bool prepare_machine_cmos(const machine_configuration &cfg,
                          const std::filesystem::path &path,
                          bool partos_layout,
                          std::string &error)
{
    try {
        std::array<uint8_t, 8> cmos{{0xF0, 0x98, 0xFF, 0x01, 0x85, 0x07, 0x00, 0x57}};
        if (std::filesystem::exists(path)) {
            if (!read_cmos_image(path, cmos, error))
                return false;
        }

        if (partos_layout) {
            cmos[1] = static_cast<uint8_t>(
                (static_cast<uint8_t>(cfg.floppies[0].image.empty() ? floppy_media_type::free : cfg.floppies[0].type) << 6) |
                (static_cast<uint8_t>(cfg.floppies[1].image.empty() ? floppy_media_type::free : cfg.floppies[1].type) << 4));
            const auto hdd_type = cfg.hard_disk.image.empty() ? hard_disk_type::free : cfg.hard_disk.type;
            cmos[2] = static_cast<uint8_t>(static_cast<uint8_t>(hdd_type) << 6);
            cmos[3] = static_cast<uint8_t>((0u << 6) |
                (partos_sio_value(cfg.sio[1].kind) << 4) |
                (partos_sio_value(cfg.sio[2].kind) << 2) |
                partos_sio_value(cfg.sio[3].kind));
            cmos[4] = static_cast<uint8_t>(
                (partos_pio_value(cfg.pio[0].kind) << 6) |
                (partos_pio_value(cfg.pio[1].kind) << 4));
            stamp_checksum(cmos);
        } else if (cfg.partner_cpm_cmos.configured) {
            const auto &settings = cfg.partner_cpm_cmos;
            cmos[1] = static_cast<uint8_t>(((settings.year / 10) << 4) |
                                           (settings.year % 10));
            cmos[3] = static_cast<uint8_t>(
                (static_cast<uint8_t>(settings.terminal) << 4) |
                static_cast<uint8_t>(settings.language));
            cmos[4] = settings.screen_columns == 80 ? 0x51 : 0x85;
            cmos[6] = static_cast<uint8_t>((cmos[6] & 0xF8) |
                (settings.reverse_video ? 0x01 : 0x00) |
                (settings.line_wrap ? 0x02 : 0x00) |
                (settings.auto_newline ? 0x04 : 0x00));
            cmos[7] = static_cast<uint8_t>(cmos[7] & ~(0x80 | 0x20 | 0x08));
            if (settings.keyboard_layout == partner_keyboard_layout::qwertz)
                cmos[7] |= 0x80;
            if (!settings.autorepeat)
                cmos[7] |= 0x20;
            if (!settings.key_click)
                cmos[7] |= 0x08;
        }

        if (!path.parent_path().empty())
            std::filesystem::create_directories(path.parent_path());
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output) { error = "cannot write CMOS file: " + path.string(); return false; }
        output.write(reinterpret_cast<const char *>(cmos.data()), cmos.size());
        if (!output) { error = "cannot finish CMOS file: " + path.string(); return false; }
        error.clear();
        return true;
    } catch (const std::exception &e) {
        error = std::string("cannot prepare CMOS: ") + e.what();
        return false;
    }
}
