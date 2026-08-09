// main.cpp - Partner emulator entry point
#include "partner_crt.hpp"
#include "partner_gdp.hpp"
#include "debugger.hpp"
#include "dap/dap_debugger.hpp"
#include "gui/gui.hpp"
#include "gui/display.hpp"
#include <iostream>
#include <string>
#include <cstring>
#include <filesystem>
#include <memory>
#include <cctype>
#include <cstdlib>
#include <vector>

static constexpr uint32_t CPU_CLOCK_HZ = 4000000;
static constexpr uint32_t TARGET_FPS = 60;
static constexpr uint32_t TICKS_PER_FRAME = CPU_CLOCK_HZ / TARGET_FPS;
static constexpr uint32_t RUN_TICK_SLICE = 8192;

namespace {

constexpr const char *DEFAULT_PARTOS_ROM = "" PARTOS_ROOT "/bin/partos.rom";
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

} // namespace

void print_usage(const char *prog)
{
    std::cerr << "Usage: " << prog << " [options]\n";
    std::cerr << "Options:\n";
    std::cerr << "  --help           Show this help\n";
    std::cerr << "  --rom FILE       ROM file (default: " << DEFAULT_PARTOS_ROM << ")\n";
    std::cerr << "  --fd0 FILE       Floppy drive 0 image\n";
    std::cerr << "  --fd1 FILE       Floppy drive 1 image\n";
    std::cerr << "  --hdd FILE       Hard disk image for Xebec/SASI controller\n";
    std::cerr << "                   (auto-attached for the default PartOS ROM)\n";
    std::cerr << "  --boot TYPE      Firmware boot target: default|floppy\n";
    std::cerr << "  --nvram FILE     Shadow MM58167 NVRAM backing file (default selected by ROM)\n";
    std::cerr << "  --terminal TYPE  Terminal profile: vt52|vt100\n";
    std::cerr << "  --model TYPE     Machine model: crt|gdp|auto (default: auto)\n";
    std::cerr << "  --dap PORT       Start the udap DAP debug server on 127.0.0.1:PORT\n";
}

int main(int argc, char **argv)
{
    std::string rom_file  = DEFAULT_PARTOS_ROM;
    std::string fd0_file = "disks/fdd-partner-p.img";
    std::string fd1_file;
    std::string hdd_file;
    std::string nvram_file = DEFAULT_PARTOS_NVRAM;
    std::string model = "auto";
    uint16_t dap_port = 0;
    bool rom_explicit = false;
    bool fd0_explicit = false;
    bool fd1_explicit = false;
    bool nvram_explicit = false;
    bool auto_boot_floppy = false;
    bool terminal_explicit = false;
    terminal_profile term_profile = terminal_profile::vt52;

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
            rom_explicit = true;
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
        auto resolve_existing_path = [&](const std::string &input) -> std::string {
            if (input.empty())
                return input;

            namespace fs = std::filesystem;
            fs::path p(input);
            if (p.is_absolute())
                return p.lexically_normal().string();

            const fs::path exe_path = fs::absolute(fs::path(argv[0])).lexically_normal();
            const fs::path exe_dir = exe_path.parent_path();

            std::vector<fs::path> candidates;
            candidates.push_back(fs::current_path() / p);
            candidates.push_back(exe_dir / p);
            if (exe_dir.filename() == "bin")
                candidates.push_back(exe_dir.parent_path() / p);

            std::error_code ec;
            for (const fs::path &candidate : candidates)
            {
                ec.clear();
                if (fs::exists(candidate, ec) && !ec)
                    return candidate.lexically_normal().string();
            }
            return input;
        };

        auto resolve_runtime_path = [&](const std::string &input) -> std::string {
            if (input.empty())
                return input;

            namespace fs = std::filesystem;
            fs::path p(input);
            if (p.is_absolute())
                return p.lexically_normal().string();

            const fs::path exe_path = fs::absolute(fs::path(argv[0])).lexically_normal();
            const fs::path exe_dir = exe_path.parent_path();

            std::vector<fs::path> candidates;
            candidates.push_back(fs::current_path() / p);
            candidates.push_back(exe_dir / p);
            if (exe_dir.filename() == "bin")
                candidates.push_back(exe_dir.parent_path() / p);

            std::error_code ec;
            for (const fs::path &candidate : candidates)
            {
                fs::path parent = candidate.parent_path();
                if (parent.empty())
                    parent = fs::current_path();
                ec.clear();
                if (fs::exists(parent, ec) && !ec)
                    return candidate.lexically_normal().string();
            }

            return (fs::current_path() / p).lexically_normal().string();
        };

        rom_file = resolve_existing_path(rom_file);
        if (!rom_explicit && !std::filesystem::exists(rom_file))
            rom_file = resolve_existing_path(DEFAULT_LEGACY_CRT_ROM);
        fd0_file = resolve_existing_path(fd0_file);
        if (!fd1_file.empty())
            fd1_file = resolve_existing_path(fd1_file);
        if (!hdd_file.empty())
            hdd_file = resolve_existing_path(hdd_file);
        nvram_file = resolve_runtime_path(nvram_file);

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
                return resolve_existing_path(DEFAULT_PARTOS_FD0);
            const std::string preferred = resolve_existing_path(
                want_gdp ? "disks/fdd-partner-g.img" : "disks/fdd-partner-p.img");
            const std::string fallback = resolve_existing_path(
                want_gdp ? "disks/fdd-partner-p.img" : "disks/fdd-partner-g.img");
            if (std::filesystem::exists(preferred)) {
                return preferred;
            }
            if (std::filesystem::exists(fallback)) {
                return fallback;
            }
            return preferred;
        };

        auto make_emu = [&](bool want_gdp) -> std::unique_ptr<partner> {
            const std::string selected_rom = pick_rom_for_model(want_gdp);
            const std::string selected_fd0 = pick_disk_for_model(want_gdp);
            const std::string selected_fd1 = fd1_explicit ? fd1_file : std::string{};
            const std::string selected_hdd =
                !hdd_file.empty() ? hdd_file :
                (is_partos_rom_path(selected_rom) ? resolve_existing_path(DEFAULT_PARTOS_HDD)
                                                  : std::string{});
            const std::string selected_nvram =
                nvram_explicit ? nvram_file :
                resolve_existing_path(is_partos_rom_path(selected_rom)
                    ? DEFAULT_PARTOS_NVRAM
                    : DEFAULT_PARTNER_NVRAM);
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

        auto push_key = [&](uint8_t ch) {
            if (auto *crt = dynamic_cast<partner_crt *>(emu.get()))
                crt->key_input(ch);
            else if (auto *gdp = dynamic_cast<partner_gdp *>(emu.get()))
                gdp->key_input(ch);
        };
        bool auto_boot_key_sent = false;
        auto service_auto_boot = [&]() {
            if (!auto_boot_floppy || auto_boot_key_sent || !emu->is_rom_enabled())
                return;
            if (is_boot_prompt_wait(emu->get_current_pc())) {
                push_key('f');
                auto_boot_key_sent = true;
                std::cout << "[info] selected firmware floppy boot\n";
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
