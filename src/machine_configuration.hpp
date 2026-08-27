#pragma once

#include "partner.hpp"
#include "terminal/terminal_factory.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>

enum class machine_model : uint8_t {
    crt,
    gdp
};

enum class machine_boot_target : uint8_t {
    default_target,
    floppy
};

enum class floppy_media_type : uint8_t {
    free = 0,
    partner = 1,
    dos_720 = 2,
    dos_360 = 3
};

enum class hard_disk_type : uint8_t {
    free = 0,
    st506 = 1,
    st412 = 2,
    st225 = 3
};

enum class monitor_type : uint8_t {
    flat,
    green_crt,
    orange_crt,
    bw_crt,
    lcd
};

enum class partner_terminal_type : uint8_t {
    ansi = 0,
    partner = 1,
    vt52 = 2
};

enum class partner_language : uint8_t {
    us_ascii = 0,
    uk_ascii = 1,
    spanish = 2,
    french = 3,
    german = 4,
    italian = 5,
    danish = 6,
    swedish = 7,
    yugoslav = 8
};

enum class partner_keyboard_layout : uint8_t {
    qwerty,
    qwertz
};

struct floppy_configuration {
    std::string image;
    floppy_media_type type = floppy_media_type::partner;
};

struct hard_disk_configuration {
    std::string image;
    hard_disk_type type = hard_disk_type::free;
};

struct monitor_configuration {
    monitor_type type = monitor_type::flat;
    float brightness = 1.0f;
    float contrast = 1.0f;
    float bloom = 1.0f;
    float scanline_strength = 1.0f;
    float mask_strength = 1.0f;
    float vignette = 1.0f;
    float persistence = 0.78f;
};

struct partner_cpm_cmos_configuration {
    // False only while loading a pre-feature JSON or a PartOS-only profile.
    // A legacy Partner boot reads the current CMOS file and fills these values
    // before it prepares or saves the effective configuration.
    bool configured = false;
    uint8_t year = 0;
    partner_terminal_type terminal = partner_terminal_type::ansi;
    partner_language language = partner_language::yugoslav;
    uint16_t screen_columns = 132;
    bool reverse_video = false;
    bool line_wrap = false;
    bool auto_newline = false;
    partner_keyboard_layout keyboard_layout = partner_keyboard_layout::qwerty;
    bool key_click = false;
    bool autorepeat = true;
};

struct machine_configuration {
    unsigned version = 1;
    machine_model model = machine_model::crt;
    std::string rom = "roms/partner_crt.rom";
    terminal_profile terminal = terminal_profile::vt52;
    machine_boot_target boot = machine_boot_target::default_target;
    std::array<floppy_configuration, 2> floppies{{
        {"disks/fdd-partner-p.img", floppy_media_type::partner},
        {"", floppy_media_type::free}
    }};
    hard_disk_configuration hard_disk;
    std::string cmos_file = "partner_cmos.bin";
    partner_cpm_cmos_configuration partner_cpm_cmos;
    monitor_configuration monitor;
    std::array<partner::sio_device_config, 4> sio{};
    std::array<partner::pio_device_config, 2> pio{};
    uint32_t squid_payload_bytes = 112;
};

machine_configuration default_machine_configuration();

// Missing JSON members retain their values from `configuration`. This is what
// lets an older or hand-written file augment the machine's current state.
bool load_machine_configuration(const std::filesystem::path &path,
                                machine_configuration &configuration,
                                std::string &error);
bool save_machine_configuration(const std::filesystem::path &path,
                                const machine_configuration &configuration,
                                std::string &error);
bool validate_machine_configuration(const machine_configuration &configuration,
                                    std::string &error);

// Decode documented legacy Partner SET UP / CP/M preferences from an existing
// CMOS image. Invalid individual values use the supplied machine's safe
// defaults, while unknown bytes remain untouched when the image is rewritten.
bool load_partner_cpm_cmos(const std::filesystem::path &path,
                           machine_model model,
                           partner_cpm_cmos_configuration &configuration,
                           std::string &error);

// Prepare the eight CMOS shadow bytes used by Partner firmware/PartOS. PartOS
// receives hardware routing; legacy Partner receives documented CP/M user
// preferences. The layouts overlap and are deliberately never combined.
bool prepare_machine_cmos(const machine_configuration &configuration,
                          const std::filesystem::path &path,
                          bool partos_layout,
                          std::string &error);
