// main.cpp - Partner emulator entry point
#include "partner_crt.hpp"
#include "debugger.hpp"
#include "gui/gui.hpp"
#include "gui/display.hpp"
#include <iostream>
#include <string>
#include <cstring>
#include <filesystem>

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
}

int main(int argc, char **argv)
{
    std::string rom_file  = "roms/partner_crt.rom";
    std::string disk_file = "disks/boot.img";
    std::string hdd_file;
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
        partner_crt idp(term_profile);
        if (hdd_file.empty())
            idp.set_force_floppy_boot(true);
        idp.load_rom(rom_file);
        idp.load_disk(0, disk_file);
        if (!hdd_file.empty())
            idp.load_hdd(hdd_file);
        idp.reset();

        gui app_gui;
        if (!app_gui.init("Iskra Delta Partner Emulator", 1800, 680))
        {
            std::cerr << "[error] Failed to initialize GUI\n";
            return 1;
        }

        // Load the 5x7 ROM font for CRT display
        display &disp = app_gui.get_display();
        if (!disp.load_font("roms/charset_ef9365.rom"))
        {
            std::cerr << "[warning] Could not load font ROM, display will be blank\n";
        }

        bool running = true;
        bool paused = true;
        dbg_action action = dbg_action::NONE;

        std::cout << "[info] Starting emulation (paused at PC=0000)...\n";
        std::cout << "[info] F5=Run/Pause  F11=Step Into  F10=Step Over  ESC=Quit\n";

        while (running)
        {
            running = app_gui.process_events(paused, action);

            if (!paused)
            {
                // Execute roughly a frame worth of emulation, but in smaller
                // chunks so the window remains responsive.
                uint32_t ticks_left = TICKS_PER_FRAME;
                while (ticks_left > 0 && running && !paused)
                {
                    const uint32_t slice = (ticks_left > RUN_TICK_SLICE) ? RUN_TICK_SLICE : ticks_left;
                    for (uint32_t i = 0; i < slice; i++)
                    {
                        idp.tick();
                    }
                    ticks_left -= slice;

                    running = app_gui.process_events(paused, action);
                    if (!running || paused)
                        break;

                    for (uint8_t ch : app_gui.drain_keys())
                        idp.key_input(ch);
                }
            }
            else if (action == dbg_action::STEP_INTO)
            {
                // Execute one instruction: tick past current M1|RD, then until next
                while (idp.is_opdone()) idp.tick();
                while (!idp.is_opdone()) idp.tick();
                action = dbg_action::NONE;
            }
            else if (action == dbg_action::STEP_OVER)
            {
                uint16_t pc = idp.get_current_pc();
                uint8_t opcode = idp.peek_mem(pc);
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
                    // Run until we return to the next instruction (with tick limit)
                    static constexpr uint32_t STEP_OVER_LIMIT = 10000000;
                    // First leave current instruction boundary
                    while (idp.is_opdone()) idp.tick();
                    for (uint32_t i = 0; i < STEP_OVER_LIMIT; i++)
                    {
                        idp.tick();
                        if (idp.get_current_pc() == target && idp.is_opdone())
                            break;
                    }
                }
                else
                {
                    // Not a call: same as step into
                    while (idp.is_opdone()) idp.tick();
                    while (!idp.is_opdone()) idp.tick();
                }
                action = dbg_action::NONE;
            }

            // Feed keyboard input to SIO
            for (uint8_t ch : app_gui.drain_keys())
                idp.key_input(ch);

            // Render terminal to display
            idp.render_to(disp);

            app_gui.begin_frame();
            app_gui.render_panels(idp, paused, action);
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
