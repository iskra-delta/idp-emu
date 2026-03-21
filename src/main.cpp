// main.cpp - Partner emulator entry point
#include "partner_crt.hpp"
#include "partner_gdp.hpp"
#include "debugger.hpp"
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

void print_usage(const char *prog)
{
    std::cerr << "Usage: " << prog << " [options]\n";
    std::cerr << "Options:\n";
    std::cerr << "  --help           Show this help\n";
    std::cerr << "  --rom FILE       ROM file (default: roms/partner_crt.rom)\n";
    std::cerr << "  --disk FILE      Boot disk image for drive A: (default: disks/boot.img)\n";
    std::cerr << "  --hdd FILE       Hard disk image for Xebec/SASI controller\n";
    std::cerr << "                   (not loaded unless explicitly requested)\n";
    std::cerr << "  --terminal TYPE  Terminal profile: vt52|vt100\n";
    std::cerr << "  --model TYPE     Machine model: crt|gdp|auto (default: auto)\n";
}

int main(int argc, char **argv)
{
    std::string rom_file  = "roms/partner_crt.rom";
    std::string disk_file = "disks/boot.img";
    std::string hdd_file;
    std::string model = "auto";
    bool disk_explicit = false;
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
        }
        else if (strcmp(argv[i], "--disk") == 0)
        {
            if ((i + 1) >= argc) { std::cerr << "Error: --disk requires a value\n"; return 1; }
            disk_file = argv[++i];
            disk_explicit = true;
        }
        else if (strcmp(argv[i], "--hdd") == 0)
        {
            if ((i + 1) >= argc) { std::cerr << "Error: --hdd requires a value\n"; return 1; }
            hdd_file = argv[++i];
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

        rom_file = resolve_existing_path(rom_file);
        disk_file = resolve_existing_path(disk_file);
        if (!hdd_file.empty())
            hdd_file = resolve_existing_path(hdd_file);

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
            if (disk_explicit)
                return disk_file;
            const std::string preferred = resolve_existing_path(want_gdp ? "disks/bootg.img" : "disks/boot.img");
            const std::string fallback = resolve_existing_path(want_gdp ? "disks/boot.img" : "disks/bootg.img");
            if (std::filesystem::exists(preferred))
                return preferred;
            if (std::filesystem::exists(fallback))
                return fallback;
            return preferred;
        };

        auto make_emu = [&](bool want_gdp) -> std::unique_ptr<partner> {
            const std::string selected_rom = pick_rom_for_model(want_gdp);
            const std::string selected_disk = pick_disk_for_model(want_gdp);
            const bool auto_insert_floppy = hdd_file.empty() || disk_explicit;
            std::cout << "[info] model=" << (want_gdp ? "gdp" : "crt")
                      << " rom=" << selected_rom
                      << " disk=" << (auto_insert_floppy ? selected_disk : std::string("(none)"))
                      << (hdd_file.empty() ? "" : (" hdd=" + hdd_file))
                      << "\n";

            std::unique_ptr<partner> created;
            if (want_gdp)
            {
                auto gdp = std::make_unique<partner_gdp>(term_profile);
                if (hdd_file.empty())
                    gdp->set_force_floppy_boot(true);
                gdp->load_rom(selected_rom);
                if (auto_insert_floppy)
                    gdp->load_disk(0, selected_disk);
                if (!hdd_file.empty())
                    gdp->load_hdd(hdd_file);
                gdp->reset();
                created = std::move(gdp);
            }
            else
            {
                auto crt = std::make_unique<partner_crt>(term_profile);
                if (hdd_file.empty())
                    crt->set_force_floppy_boot(true);
                crt->load_rom(selected_rom);
                if (auto_insert_floppy)
                    crt->load_disk(0, selected_disk);
                if (!hdd_file.empty())
                    crt->load_hdd(hdd_file);
                crt->reset();
                created = std::move(crt);
            }
            return created;
        };

        std::unique_ptr<partner> emu = make_emu(gdp_model);

        gui app_gui;
        if (!app_gui.init("Iskra Delta Partner Emulator", 1800, 860))
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

        // Initialize display font from built-in EF9365 image.
        display &disp = app_gui.get_display();
        if (!disp.load_font(""))
        {
            std::cerr << "[warning] Could not initialize display font\n";
        }

        bool running = true;
        bool paused = true;
        dbg_action action = dbg_action::NONE;

        std::cout << "[info] Starting emulation (paused at PC=0000)...\n";
        const char *quit_hint = (term_profile == terminal_profile::vt100_ansi) ? "Ctrl+Q=Quit" : "ESC=Quit";
        std::cout << "[info] F5=Switch to GDP  Space=Run/Pause  F11=Step Into  F10=Step Over  " << quit_hint << "\n";

        auto push_key = [&](uint8_t ch) {
            if (auto *crt = dynamic_cast<partner_crt *>(emu.get()))
                crt->key_input(ch);
            else if (auto *gdp = dynamic_cast<partner_gdp *>(emu.get()))
                gdp->key_input(ch);
        };

        auto render_machine = [&]() {
            if (auto *crt = dynamic_cast<partner_crt *>(emu.get()))
                crt->render_to(disp);
            else if (auto *gdp = dynamic_cast<partner_gdp *>(emu.get()))
                gdp->render_to(disp);
            else
                disp.clear();
        };

        while (running)
        {
            running = app_gui.process_events(paused, action);

            if (action == dbg_action::SWITCH_TO_GDP)
            {
                if (!gdp_model)
                {
                    gdp_model = true;
                    emu = make_emu(true);
                    paused = true;
                    std::cout << "[info] Switched to GDP model (paused)\n";
                }
                action = dbg_action::NONE;
            }
            else if (!paused)
            {
                // Execute roughly a frame worth of emulation, but in smaller
                // chunks so the window remains responsive.
                uint32_t ticks_left = TICKS_PER_FRAME;
                while (ticks_left > 0 && running && !paused)
                {
                    const uint32_t slice = (ticks_left > RUN_TICK_SLICE) ? RUN_TICK_SLICE : ticks_left;
                    for (uint32_t i = 0; i < slice; i++)
                    {
                        emu->tick();
                    }
                    ticks_left -= slice;

                    running = app_gui.process_events(paused, action);
                    if (!running || paused)
                        break;

                    for (uint8_t ch : app_gui.drain_keys())
                        push_key(ch);
                }
            }
            else if (action == dbg_action::STEP_INTO)
            {
                // Execute one instruction: tick past current M1|RD, then until next
                while (emu->is_opdone()) emu->tick();
                while (!emu->is_opdone()) emu->tick();
                action = dbg_action::NONE;
            }
            else if (action == dbg_action::STEP_OVER)
            {
                auto step_one_instruction = [&]() {
                    // Execute one instruction: tick past current M1|RD, then until next.
                    while (emu->is_opdone())
                        emu->tick();
                    // Guard against pathological hangs during single-step.
                    static constexpr uint32_t STEP_GUARD_LIMIT = 1000000;
                    uint32_t guard = 0;
                    while (!emu->is_opdone() && guard < STEP_GUARD_LIMIT)
                    {
                        emu->tick();
                        guard++;
                    }
                };

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
                            running = app_gui.process_events(paused, action);
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
                    step_one_instruction();
                }
                if (action == dbg_action::STEP_OVER)
                    action = dbg_action::NONE;
            }

            // Feed keyboard input to SIO
            for (uint8_t ch : app_gui.drain_keys())
                push_key(ch);

            // Render terminal to display
            render_machine();

            app_gui.begin_frame();
            app_gui.render_panels(*emu, paused, action);
            app_gui.end_frame();
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
