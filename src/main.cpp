// main.cpp - Partner emulator entry point
#include "partner_crt.hpp"
#include "partner_gdp.hpp"
#include "debugger.hpp"
#include "dap/dap_debugger.hpp"
#include "gui/gui.hpp"
#include "gui/display.hpp"
#include "startup_input.hpp"
#include "runtime_paths.hpp"
#include <chrono>
#include <iostream>
#include <string>
#include <cstring>
#include <filesystem>
#include <memory>
#include <limits>
#include <optional>
#include <cctype>
#include <cstdlib>
#include <thread>
#include <vector>

static constexpr uint32_t CPU_CLOCK_HZ = 4000000;
static constexpr uint32_t TARGET_FPS = 60;
static constexpr uint32_t TICKS_PER_FRAME = CPU_CLOCK_HZ / TARGET_FPS;
static constexpr uint32_t RUN_TICK_SLICE = 8192;
static constexpr auto STARTUP_INPUT_SETTLE_TIME = std::chrono::milliseconds(400);

namespace {

constexpr const char *DEFAULT_PARTOS_NVRAM = "partos/partos_shadow_nvram.bin";
constexpr const char *DEFAULT_PARTNER_NVRAM = "partner_cmos.bin";
constexpr const char *DEFAULT_LEGACY_CRT_ROM = "roms/partner_crt.rom";
constexpr const char *DEFAULT_PARTOS_FD0 = "disks/fdd-dos.img";
constexpr const char *DEFAULT_PARTOS_HDD = "disks/hdd-dos.img";

bool is_boot_prompt_wait(uint16_t pc)
{
    return (pc == 0x009F) || (pc == 0x00A1) || (pc == 0x00A3);
}

bool is_floppy_boot_start(uint16_t pc)
{
    return (pc == 0x0292) || (pc == 0x029B) || (pc == 0x03A5) || (pc == 0x03F5);
}

bool boot_trace_enabled()
{
    static const bool enabled = [] {
        const char *value = std::getenv("IDP_TRACE_BOOT");
        return value && value[0] && value[0] != '0';
    }();
    return enabled;
}

void step_one_instruction(partner &emu)
{
    while (emu.is_opdone())
        emu.tick();

    static constexpr uint32_t STEP_GUARD_LIMIT = 1000000;
    uint32_t guard = 0;
    while (!emu.is_opdone() && guard < STEP_GUARD_LIMIT) {
        emu.tick();
        guard++;
    }
}

bool is_partos_rom_path(const std::string &path)
{
    std::string low = std::filesystem::path(path).filename().string();
    for (char &c : low)
        c = (char)std::tolower((unsigned char)c);
    return low == "partos.rom";
}

bool parse_milliseconds(const char *text, uint32_t &value)
{
    if (!text || !text[0] || text[0] == '-')
        return false;
    char *end = nullptr;
    const unsigned long parsed = std::strtoul(text, &end, 10);
    if (end == text || *end != '\0' || parsed > std::numeric_limits<uint32_t>::max())
        return false;
    value = static_cast<uint32_t>(parsed);
    return true;
}

bool parse_tcp_port(const char *text, int &value)
{
    uint32_t parsed = 0;

    if (!parse_milliseconds(text, parsed) || parsed == 0U || parsed > 65535U)
        return false;
    value = static_cast<int>(parsed);
    return true;
}

} // namespace

void print_usage(const char *prog)
{
    std::cerr << "Usage: " << prog << " [options]\n";
    std::cerr << "All options:\n";
    std::cerr << "  --help                 Show this help\n";
    std::cerr << "  --rom FILE             ROM file (default: " << DEFAULT_LEGACY_CRT_ROM << ")\n";
    std::cerr << "  --fd0 FILE             Floppy drive 0 image\n";
    std::cerr << "  --disk FILE            Alias for --fd0\n";
    std::cerr << "  --fd1 FILE             Floppy drive 1 image\n";
    std::cerr << "  --disk-b FILE          Alias for --fd1\n";
    std::cerr << "  --hdd FILE             Hard disk image for Xebec/SASI controller\n";
    std::cerr << "                         (auto-attached for the default PartOS ROM)\n";
    std::cerr << "  --boot TYPE            Firmware boot target: default|floppy\n";
    std::cerr << "  --nvram FILE           Shadow MM58167 NVRAM backing file\n";
    std::cerr << "                         (default selected by ROM)\n";
    std::cerr << "  --terminal TYPE        Terminal profile: vt52|vt100|ansi\n";
    std::cerr << "  --model TYPE           Machine model: crt|gdp|auto (default: auto)\n";
    std::cerr << "  --covox-port PORT      Attach Covox to main PIO: 1=A, 2=B\n";
    std::cerr << "  --sio-tcp PORT DATA CONTROL\n";
    std::cerr << "                         Attach PAKET port 2, 3, or 4 to a TCP bridge\n";
    std::cerr << "  --dap PORT             Start the udap DAP server on 127.0.0.1:PORT\n";
    std::cerr << "  --commands TEXT        Type TEXT after the GUI opens; may be repeated\n";
    std::cerr << "  --command TEXT         Alias for --commands\n";
    std::cerr << "  --type TEXT            Alias for --commands\n";
    std::cerr << "                         Escapes: \\n=Enter, \\r=Enter, \\t=Tab, \\b=Backspace,\n";
    std::cerr << "                         \\e=Esc, \\\\, quotes, and \\xNN\n";
    std::cerr << "  --type-delay MS        Delay before the first startup key (default: 1000)\n";
    std::cerr << "  --type-interval MS     Delay between startup keys (default: 350)\n";
    std::cerr << "  --type-enter-delay MS  Delay after Enter when more keys follow (default: interval)\n";
}

int main(int argc, char **argv)
{
    std::string rom_file = DEFAULT_LEGACY_CRT_ROM;
    std::string fd0_file = "disks/fdd-partner-p.img";
    std::string fd1_file;
    std::string hdd_file;
    std::string nvram_file = DEFAULT_PARTOS_NVRAM;
    std::string model = "auto";
    uint16_t dap_port = 0;
    int covox_port = 0;
    int sio_tcp_port = 0;
    int sio_tcp_data_port = 0;
    int sio_tcp_control_port = 0;
    bool fd0_explicit = false;
    bool fd1_explicit = false;
    bool nvram_explicit = false;
    bool auto_boot_floppy = false;
    bool terminal_explicit = false;
    terminal_profile term_profile = terminal_profile::vt52;
    std::vector<uint8_t> startup_keys;
    uint32_t startup_delay_ms = 1000;
    uint32_t startup_interval_ms = 350;
    uint32_t startup_enter_delay_ms = 350;
    bool startup_enter_delay_explicit = false;

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--help") == 0)
        {
            print_usage(argv[0]);
            return 0;
        }
        else if (strcmp(argv[i], "--rom") == 0)
        {
            if ((i + 1) >= argc) { std::cerr << "Error: --rom requires a value\n"; return 1; }
            rom_file = argv[++i];
        }
        else if (strcmp(argv[i], "--fd0") == 0 || strcmp(argv[i], "--disk") == 0)
        {
            if ((i + 1) >= argc) { std::cerr << "Error: --fd0 requires a value\n"; return 1; }
            fd0_file = argv[++i];
            fd0_explicit = true;
        }
        else if (strcmp(argv[i], "--fd1") == 0 || strcmp(argv[i], "--disk-b") == 0)
        {
            if ((i + 1) >= argc) { std::cerr << "Error: --fd1 requires a value\n"; return 1; }
            fd1_file = argv[++i];
            fd1_explicit = true;
        }
        else if (strcmp(argv[i], "--hdd") == 0)
        {
            if ((i + 1) >= argc) { std::cerr << "Error: --hdd requires a value\n"; return 1; }
            hdd_file = argv[++i];
        }
        else if (strcmp(argv[i], "--boot") == 0)
        {
            if ((i + 1) >= argc)
            {
                std::cerr << "Error: --boot requires a value: default|floppy\n";
                return 1;
            }
            const char *value = argv[++i];
            if (strcmp(value, "default") == 0)
                auto_boot_floppy = false;
            else if (strcmp(value, "floppy") == 0)
                auto_boot_floppy = true;
            else
            {
                std::cerr << "Error: Unknown boot target: " << value << "\n";
                std::cerr << "Valid values: default, floppy\n";
                return 1;
            }
        }
        else if (strcmp(argv[i], "--nvram") == 0)
        {
            if ((i + 1) >= argc) { std::cerr << "Error: --nvram requires a value\n"; return 1; }
            nvram_file = argv[++i];
            nvram_explicit = true;
        }
        else if (strcmp(argv[i], "--terminal") == 0)
        {
            if ((i + 1) >= argc)
            {
                std::cerr << "Error: --terminal requires a value: vt52|vt100\n";
                return 1;
            }
            const char *value = argv[++i];
            if (strcmp(value, "vt52") == 0)
                term_profile = terminal_profile::vt52;
            else if (strcmp(value, "vt100") == 0 || strcmp(value, "ansi") == 0)
                term_profile = terminal_profile::vt100_ansi;
            else
            {
                std::cerr << "Error: Unknown terminal profile: " << value << "\n";
                std::cerr << "Valid values: vt52, vt100\n";
                return 1;
            }
            terminal_explicit = true;
        }
        else if (strcmp(argv[i], "--dap") == 0)
        {
            if ((i + 1) >= argc) { std::cerr << "Error: --dap requires a port number\n"; return 1; }
            char *end = nullptr;
            const unsigned long parsed = std::strtoul(argv[++i], &end, 10);
            if (end == argv[i] || *end != '\0' || parsed == 0 || parsed > 65535UL)
            {
                std::cerr << "Error: Invalid --dap port: " << argv[i] << "\n";
                return 1;
            }
            dap_port = (uint16_t)parsed;
        }
        else if (strcmp(argv[i], "--covox-port") == 0)
        {
            if ((i + 1) >= argc)
            {
                std::cerr << "Error: --covox-port requires 1 (PIO A) or 2 (PIO B)\n";
                return 1;
            }
            const char *value = argv[++i];
            if (strcmp(value, "1") == 0)
                covox_port = 1;
            else if (strcmp(value, "2") == 0)
                covox_port = 2;
            else
            {
                std::cerr << "Error: Invalid --covox-port value: " << value
                          << "; use 1 or 2\n";
                return 1;
            }
        }
        else if (strcmp(argv[i], "--sio-tcp") == 0)
        {
            if ((i + 3) >= argc)
            {
                std::cerr << "Error: --sio-tcp requires PORT DATA CONTROL\n";
                return 1;
            }
            const char *port = argv[++i];
            if ((port[1] != '\0') || (port[0] < '2') || (port[0] > '4'))
            {
                std::cerr << "Error: --sio-tcp PORT must be 2, 3, or 4\n";
                return 1;
            }
            sio_tcp_port = port[0] - '0';
            if (!parse_tcp_port(argv[++i], sio_tcp_data_port) ||
                !parse_tcp_port(argv[++i], sio_tcp_control_port) ||
                (sio_tcp_data_port == sio_tcp_control_port))
            {
                std::cerr << "Error: --sio-tcp DATA and CONTROL must be distinct TCP ports\n";
                return 1;
            }
        }
        else if (strcmp(argv[i], "--commands") == 0 ||
                 strcmp(argv[i], "--command") == 0 ||
                 strcmp(argv[i], "--type") == 0)
        {
            if ((i + 1) >= argc)
            {
                std::cerr << "Error: " << argv[i] << " requires text to type\n";
                return 1;
            }
            std::string decode_error;
            if (!startup_input::decode(argv[++i], startup_keys, decode_error))
            {
                std::cerr << "Error: " << decode_error << "\n";
                return 1;
            }
        }
        else if (strcmp(argv[i], "--type-delay") == 0 ||
                 strcmp(argv[i], "--type-interval") == 0 ||
                 strcmp(argv[i], "--type-enter-delay") == 0)
        {
            const bool is_delay = strcmp(argv[i], "--type-delay") == 0;
            const bool is_enter_delay =
                strcmp(argv[i], "--type-enter-delay") == 0;
            const char *option = argv[i];
            if ((i + 1) >= argc)
            {
                std::cerr << "Error: " << option << " requires milliseconds\n";
                return 1;
            }
            uint32_t &destination = is_delay ? startup_delay_ms :
                (is_enter_delay ? startup_enter_delay_ms : startup_interval_ms);
            if (!parse_milliseconds(argv[++i], destination))
            {
                std::cerr << "Error: Invalid " << option << " value: " << argv[i] << "\n";
                return 1;
            }
            if (is_enter_delay)
                startup_enter_delay_explicit = true;
        }
        else if (strcmp(argv[i], "--model") == 0)
        {
            if ((i + 1) >= argc)
            {
                std::cerr << "Error: --model requires a value: crt|gdp|auto\n";
                return 1;
            }
            model = argv[++i];
            if (model != "crt" && model != "gdp" && model != "auto")
            {
                std::cerr << "Error: Unknown model: " << model << "\n";
                return 1;
            }
        }
        else
        {
            std::cerr << "Error: Unknown option: " << argv[i] << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    try
    {
        auto resolve_existing_path = [](const std::string &input) {
            return runtime_paths::find_resource(input);
        };

        rom_file = resolve_existing_path(rom_file);
        fd0_file = resolve_existing_path(fd0_file);
        if (!fd1_file.empty())
            fd1_file = resolve_existing_path(fd1_file);
        if (!hdd_file.empty())
            hdd_file = resolve_existing_path(hdd_file);
        if (nvram_explicit)
            nvram_file = std::filesystem::absolute(nvram_file).lexically_normal().string();

        bool gdp_model = false;
        if (model == "gdp")
            gdp_model = true;
        else if (model == "auto")
        {
            std::string low = rom_file;
            for (char &c : low) c = (char)std::tolower((unsigned char)c);
            gdp_model = (low.find("gdp") != std::string::npos);
        }

        // Pick sensible default terminal when user doesn't force one:
        // CRT -> VT52, GDP -> VT100.
        if (!terminal_explicit)
        {
            term_profile = gdp_model ? terminal_profile::vt100_ansi : terminal_profile::vt52;
        }

        auto pick_rom_for_model = [&](bool want_gdp) -> std::string {
            if (!want_gdp)
                return rom_file;

            std::string candidate = rom_file;
            std::string low = candidate;
            for (char &c : low) c = (char)std::tolower((unsigned char)c);
            if (low.find("crt") == std::string::npos)
                return rom_file;
            const size_t crt_pos = low.find("crt");
            if (crt_pos != std::string::npos)
            {
                candidate.replace(crt_pos, 3, "gdp");
                if (std::filesystem::exists(candidate))
                    return candidate;
            }
            const std::string fallback = resolve_existing_path("roms/partner_gdp.rom");
            if (std::filesystem::exists(fallback))
                return fallback;
            return rom_file;
        };

        auto pick_disk_for_model = [&](bool want_gdp) -> std::string {
            if (fd0_explicit)
                return fd0_file;
            if (is_partos_rom_path(rom_file))
                return runtime_paths::mutable_resource_copy(DEFAULT_PARTOS_FD0);
            const std::string preferred_name =
                want_gdp ? "disks/fdd-partner-g.img" : "disks/fdd-partner-p.img";
            const std::string fallback_name =
                want_gdp ? "disks/fdd-partner-p.img" : "disks/fdd-partner-g.img";
            const std::string preferred = resolve_existing_path(preferred_name);
            const std::string fallback = resolve_existing_path(fallback_name);
            if (std::filesystem::exists(preferred)) {
                return runtime_paths::mutable_resource_copy(preferred_name);
            }
            if (std::filesystem::exists(fallback)) {
                return runtime_paths::mutable_resource_copy(fallback_name);
            }
            return preferred;
        };

        auto make_emu = [&](bool want_gdp) -> std::unique_ptr<partner> {
            const std::string selected_rom = pick_rom_for_model(want_gdp);
            const std::string selected_fd0 = pick_disk_for_model(want_gdp);
            const std::string selected_fd1 = fd1_explicit ? fd1_file : std::string{};
            const std::string selected_hdd =
                !hdd_file.empty() ? hdd_file :
                (is_partos_rom_path(selected_rom) ? runtime_paths::mutable_resource_copy(DEFAULT_PARTOS_HDD)
                                                  : std::string{});
            const std::string selected_nvram =
                nvram_explicit ? nvram_file :
                runtime_paths::user_file(is_partos_rom_path(selected_rom)
                    ? DEFAULT_PARTOS_NVRAM : DEFAULT_PARTNER_NVRAM).string();
            const bool auto_insert_floppy = selected_hdd.empty() || fd0_explicit || is_partos_rom_path(selected_rom);
            std::cout << "[info] model=" << (want_gdp ? "gdp" : "crt")
                      << " rom=" << selected_rom
                      << " fd0=" << (auto_insert_floppy ? selected_fd0 : std::string("(none)"))
                      << " fd1=" << (selected_fd1.empty() ? std::string("(none)") : selected_fd1)
                      << (selected_hdd.empty() ? "" : (" hdd=" + selected_hdd))
                      << " nvram=" << selected_nvram
                      << "\n";

            std::unique_ptr<partner> created;
            if (want_gdp)
            {
                auto gdp = std::make_unique<partner_gdp>(term_profile, selected_nvram);
                gdp->load_rom(selected_rom);
                if (auto_insert_floppy)
                    gdp->load_disk(0, selected_fd0);
                if (!selected_fd1.empty())
                    gdp->load_disk(1, selected_fd1);
                if (!selected_hdd.empty())
                    gdp->load_hdd(selected_hdd);
                gdp->reset();
                created = std::move(gdp);
            }
            else
            {
                auto crt = std::make_unique<partner_crt>(term_profile, selected_nvram);
                crt->load_rom(selected_rom);
                if (auto_insert_floppy)
                    crt->load_disk(0, selected_fd0);
                if (!selected_fd1.empty())
                    crt->load_disk(1, selected_fd1);
                if (!selected_hdd.empty())
                    crt->load_hdd(selected_hdd);
                crt->reset();
                created = std::move(crt);
            }
            return created;
        };

        std::unique_ptr<partner> emu = make_emu(gdp_model);
        if (covox_port != 0)
        {
            const auto port = covox_port == 1 ? partner::pio_port_id::a
                                              : partner::pio_port_id::b;
            partner::pio_device_config config;
            config.kind = partner::pio_device_kind::covox;
            emu->set_pio_device_config(port, config);
            std::cout << "[info] Covox attached to main PIO "
                      << (covox_port == 1 ? "A" : "B")
                      << " (program argument " << covox_port << ")\n";
        }
        if (sio_tcp_port != 0)
        {
            const auto port = sio_tcp_port == 2 ? partner::sio_port_id::sio1_b :
                (sio_tcp_port == 3 ? partner::sio_port_id::sio2_a
                                   : partner::sio_port_id::sio2_b);
            partner::sio_device_config config;
            config.kind = partner::sio_device_kind::tcp_bridge;
            config.tcp_data_port = sio_tcp_data_port;
            config.tcp_control_port = sio_tcp_control_port;
            config.tcp_require_rts = true;
            config.tcp_cts_follows_data_client = true;
            if (!emu->set_sio_device_config(port, config))
            {
                std::cerr << "Error: could not attach TCP bridge to SIO port\n";
                return 1;
            }
            std::cout << "[info] PAKET port " << sio_tcp_port
                      << " attached to TCP " << sio_tcp_data_port
                      << "/" << sio_tcp_control_port << "\n";
        }

        gui app_gui;
        dap_debugger remote_dbg;
        if (!app_gui.init("Iskra Delta Partner Emulator", 1100, 720))
        {
            std::cerr << "[error] Failed to initialize GUI\n";
            const char *disp = std::getenv("DISPLAY");
            const char *wayland = std::getenv("WAYLAND_DISPLAY");
            const char *xdg = std::getenv("XDG_SESSION_TYPE");
            std::cerr << "[info] DISPLAY=" << (disp ? disp : "(unset)")
                      << " WAYLAND_DISPLAY=" << (wayland ? wayland : "(unset)")
                      << " XDG_SESSION_TYPE=" << (xdg ? xdg : "(unset)") << "\n";
            return 1;
        }
        app_gui.set_terminal_profile(term_profile);
        app_gui.set_remote_debugger(&remote_dbg);

        if (dap_port != 0)
        {
            std::string dap_error;
            if (!remote_dbg.start(*emu, "127.0.0.1", dap_port, &dap_error))
                std::cerr << "[warning] Could not start DAP server: " << dap_error << "\n";
        }

        // Initialize display font from built-in EF9365 image.
        display &disp = app_gui.get_display();
        if (!disp.load_font(""))
        {
            std::cerr << "[warning] Could not initialize display font\n";
        }

        bool running = true;
        bool paused = false;  // start running by default; press Space to pause
        bool resume_skip_bp = false;
        dbg_action action = dbg_action::NONE;

        std::cout << "[info] Starting emulation...\n";
        const char *quit_hint = "Ctrl+Q=Quit";
        std::cout << "[info] Space=Run/Pause  F11=Step Into  F10=Step Over  " << quit_hint << "\n";

        using frame_clock = std::chrono::steady_clock;
        const auto host_frame_period =
            std::chrono::nanoseconds(1000000000ULL / TARGET_FPS);
        auto next_host_frame = frame_clock::now() + host_frame_period;
        uint32_t frame_tick_remainder = 0;

        auto push_key = [&](uint8_t ch) -> bool {
            if (auto *crt = dynamic_cast<partner_crt *>(emu.get()))
            {
                crt->key_input(ch);
                return true;
            }
            else if (auto *gdp = dynamic_cast<partner_gdp *>(emu.get()))
                return gdp->key_input(ch);
            return false;
        };
        const auto keyboard_input_ready = [&]() {
            if (const auto *crt = dynamic_cast<const partner_crt *>(emu.get()))
                return crt->keyboard_input_ready();
            if (const auto *gdp = dynamic_cast<const partner_gdp *>(emu.get()))
                return gdp->keyboard_input_ready();
            return false;
        };
        const auto pending_key_count = [&]() -> size_t {
            if (const auto *crt = dynamic_cast<const partner_crt *>(emu.get()))
                return crt->pending_key_count();
            if (const auto *gdp = dynamic_cast<const partner_gdp *>(emu.get()))
                return gdp->pending_key_count();
            return 0;
        };
        const auto terminal_text = [&]() {
            if (const auto *crt = dynamic_cast<const partner_crt *>(emu.get()))
                return crt->dump_terminal_text();
            if (const auto *gdp = dynamic_cast<const partner_gdp *>(emu.get()))
                return gdp->dump_terminal_text();
            return std::string{};
        };
        if (!startup_enter_delay_explicit)
            startup_enter_delay_ms = startup_interval_ms;
        startup_input scripted_input(
            std::move(startup_keys),
            std::chrono::milliseconds(startup_delay_ms),
            std::chrono::milliseconds(startup_interval_ms),
            std::chrono::milliseconds(startup_enter_delay_ms));
        bool scripted_input_started = false;
        bool scripted_first_key_sent = false;
        bool scripted_input_complete_reported = false;
        std::string scripted_screen_snapshot;
        auto scripted_screen_changed_at = startup_input::clock::now();
        const auto service_scripted_input = [&]() {
            if (!scripted_input_started || scripted_input.finished())
                return;

            const auto now = startup_input::clock::now();
            const std::string current_screen = terminal_text();
            if (current_screen != scripted_screen_snapshot)
            {
                scripted_screen_snapshot = current_screen;
                scripted_screen_changed_at = now;
            }

            // Firmware commonly enables the keyboard SIO briefly and resets it
            // again while booting. Wait for a quiet screen before the first
            // scripted byte so that byte cannot disappear in the later reset.
            if (!keyboard_input_ready() || pending_key_count() != 0u)
                return;
            if (!scripted_first_key_sent &&
                now - scripted_screen_changed_at < STARTUP_INPUT_SETTLE_TIME)
                return;

            if (const std::optional<uint8_t> key = scripted_input.take_due(now))
            {
                if (!push_key(*key))
                    return;
                scripted_first_key_sent = true;
                if (scripted_input.finished() && !scripted_input_complete_reported)
                {
                    scripted_input_complete_reported = true;
                    std::cout << "[info] startup command typing complete\n";
                }
            }
        };
        bool auto_boot_key_sent = false;
        auto service_auto_boot = [&]() {
            if (!auto_boot_floppy || auto_boot_key_sent || !emu->is_rom_enabled())
                return;
            if (is_boot_prompt_wait(emu->get_current_pc())) {
                if (push_key('f')) {
                    auto_boot_key_sent = true;
                    std::cout << "[info] selected firmware floppy boot\n";
                }
            }
        };

        auto render_machine = [&]() {
            if (auto *crt = dynamic_cast<partner_crt *>(emu.get()))
                crt->render_to(disp);
            else if (auto *gdp = dynamic_cast<partner_gdp *>(emu.get()))
                gdp->render_to(disp);
            else
                disp.clear();
        };
        uint64_t next_boot_trace_tick = 1;
        auto service_boot_trace = [&]() {
            if (!boot_trace_enabled())
                return;

            const uint64_t ticks = emu->get_tick_count();
            if (ticks < next_boot_trace_tick)
                return;

            const uint16_t pc = emu->get_current_pc();
            std::cout << "[boot] tick=" << ticks
                      << " pc=0x" << std::hex << pc << std::dec
                      << " prompt=" << (is_boot_prompt_wait(pc) ? 1 : 0)
                      << " floppy_start=" << (is_floppy_boot_start(pc) ? 1 : 0)
                      << std::endl;
            next_boot_trace_tick = ticks + 2000000ULL;
        };

        while (running)
        {
            {
                std::unique_lock<std::recursive_mutex> emu_lock(remote_dbg.mutex());
                const bool was_paused = paused;
                running = app_gui.process_events(*emu, paused, action);
                service_scripted_input();
                remote_dbg.sync_paused_state(paused);
                // User paused from the GUI while a client continue was
                // running: tell the client. (Transition only - a pending,
                // not-yet-started continue must not be reported as stopped.)
                if (paused && !was_paused && remote_dbg.continue_active())
                    remote_dbg.notify_stopped("pause");
            }

            if (auto request = app_gui.take_remote_debugger_request()) {
                std::string error;
                bool ok = false;
                if (request->action == gui::remote_debugger_request::kind::start) {
                    ok = remote_dbg.start(*emu, request->host, request->port, &error);
                    if (!ok && error.empty())
                        error = "Failed to start debug server.";
                } else {
                    ok = remote_dbg.stop(&error);
                    if (!ok && error.empty())
                        error = "Failed to stop debug server.";
                }
                app_gui.set_remote_debugger_error(ok ? std::string{} : error);
            }

            {
                std::unique_lock<std::recursive_mutex> emu_lock(remote_dbg.mutex());
                remote_dbg.sync_paused_state(paused);
                const bool was_paused_at_frame_start = paused;

                if (remote_dbg.take_pending_command() ==
                    dap_debugger::pending_command::continue_execution) {
                    paused = false;
                    remote_dbg.sync_paused_state(paused);
                }

                if (!paused && remote_dbg.pause_requested()) {
                    paused = true;
                    remote_dbg.complete_pause();
                }

                // When resuming from a stop, skip the breakpoint test for the
                // instruction we are standing on, or a continue from a hit
                // breakpoint would re-trigger it without making progress.
                if (!paused && was_paused_at_frame_start)
                    resume_skip_bp = true;

                if (!paused)
                {
                    // Execute roughly a frame worth of emulation, but in smaller
                    // chunks so the window remains responsive.
                    uint32_t ticks_left = TICKS_PER_FRAME;
                    frame_tick_remainder += CPU_CLOCK_HZ % TARGET_FPS;
                    if (frame_tick_remainder >= TARGET_FPS)
                    {
                        ++ticks_left;
                        frame_tick_remainder -= TARGET_FPS;
                    }
                    while (ticks_left > 0 && running && !paused)
                    {
                        const uint32_t slice = (ticks_left > RUN_TICK_SLICE) ? RUN_TICK_SLICE : ticks_left;
                        for (uint32_t i = 0; i < slice && !paused; i++)
                        {
                            if (emu->is_opdone()) {
                                // Honour a debugger pause before the next
                                // instruction executes; instruction-level
                                // latency keeps launch/pause sound even when
                                // the GUI frame rate degrades.
                                if (remote_dbg.pause_requested()) {
                                    paused = true;
                                    remote_dbg.complete_pause();
                                    break;
                                }
                                if (resume_skip_bp) {
                                    resume_skip_bp = false;
                                } else if (remote_dbg.session_active() &&
                                           remote_dbg.has_breakpoint(emu->get_current_pc())) {
                                    paused = true;
                                    remote_dbg.notify_stopped("breakpoint");
                                    break;
                                }
                            }

                            emu->tick();
                            service_auto_boot();
                            service_boot_trace();

                        }
                        ticks_left -= slice;
                        remote_dbg.sync_paused_state(paused);

                        if (!running || paused)
                            break;

                        running = app_gui.process_events(*emu, paused, action);
                        remote_dbg.sync_paused_state(paused);
                        if (paused && remote_dbg.continue_active())
                            remote_dbg.notify_stopped("pause");
                        if (!running || paused)
                            break;

                        for (uint8_t ch : app_gui.drain_keys())
                            push_key(ch);
                    }
                }
                if (paused && action == dbg_action::STEP_INTO)
                {
                    step_one_instruction(*emu);
                    action = dbg_action::NONE;
                }
                else if (paused && action == dbg_action::STEP_OVER)
                {
                    uint16_t pc = emu->get_current_pc();
                    uint8_t opcode = emu->peek_mem(pc);
                    if (is_call_or_rst(opcode))
                    {
                        // Find the address of the next instruction after the CALL/RST
                        uint16_t target;
                        if ((opcode & 0xC7) == 0xC7) {
                            // RST: 1-byte instruction
                            target = pc + 1;
                        } else {
                            // CALL variants: 3-byte instruction
                            target = pc + 3;
                        }
                        // Run until we return to the next instruction (with tick limit),
                        // while pumping events so UI/debugger stays responsive.
                        static constexpr uint32_t STEP_OVER_LIMIT = 10000000;
                        static constexpr uint32_t STEP_OVER_EVENT_SLICE = 8192;

                        // First leave current instruction boundary.
                        while (emu->is_opdone())
                            emu->tick();

                        uint32_t i = 0;
                        for (; i < STEP_OVER_LIMIT; i++)
                        {
                            emu->tick();
                            if (emu->get_current_pc() == target && emu->is_opdone())
                                break;

                            if ((i % STEP_OVER_EVENT_SLICE) == 0)
                            {
                                // Keep window responsive during long step-over spans.
                                running = app_gui.process_events(*emu, paused, action);
                                if (!running)
                                    break;
                                // Defer any new debugger action to outer loop.
                                if (action != dbg_action::STEP_OVER)
                                    break;
                            }
                        }
                        if (i >= STEP_OVER_LIMIT)
                        {
                            std::cerr << "[warn] Step Over timeout at PC="
                                      << std::hex << emu->get_current_pc() << std::dec
                                      << " (call did not return within limit)\n";
                        }
                    }
                    else
                    {
                        // Not a call: same as step into
                        step_one_instruction(*emu);
                    }
                    if (action == dbg_action::STEP_OVER)
                        action = dbg_action::NONE;
                }

                // Feed keyboard input to SIO
                for (uint8_t ch : app_gui.drain_keys())
                    push_key(ch);

                // Render terminal to display
                render_machine();
            }

            app_gui.begin_frame();
            {
                std::unique_lock<std::recursive_mutex> emu_lock(remote_dbg.mutex());
                app_gui.render_panels(*emu, paused, action);
            }
            app_gui.end_frame();

            // TICKS_PER_FRAME represents exactly one 60 Hz slice of the
            // Partner's 4 MHz clock. Pace those slices against steady host
            // time even when OpenGL vsync is disabled, or sound and the whole
            // guest run faster than the physical machine. Do not accumulate a
            // large catch-up burst after a debugger stop or host stall.
            const auto host_now = frame_clock::now();
            if (host_now < next_host_frame)
            {
                std::this_thread::sleep_until(next_host_frame);
                next_host_frame += host_frame_period;
            }
            else if (host_now - next_host_frame > host_frame_period * 4)
            {
                next_host_frame = host_now + host_frame_period;
            }
            else
            {
                next_host_frame += host_frame_period;
            }
            if (!scripted_input_started && !scripted_input.empty())
            {
                scripted_input.start(startup_input::clock::now());
                scripted_input_started = true;
                scripted_screen_changed_at = startup_input::clock::now();
                std::cout << "[info] queued " << scripted_input.size()
                          << " startup key(s), first key in " << startup_delay_ms
                          << " ms\n";
            }
        }

        std::string remote_dbg_stop_error;
        if (!remote_dbg.stop(&remote_dbg_stop_error) && !remote_dbg_stop_error.empty()) {
            std::cerr << "[warning] Failed to stop remote debugger: "
                      << remote_dbg_stop_error << "\n";
        }

        app_gui.shutdown();
    }
    catch (const std::exception &e)
    {
        std::cerr << "[error] " << e.what() << "\n";
        return 1;
    }

    return 0;
}
