// main.cpp - Partner emulator entry point
#define CHIPS_IMPL
#include "z80.h"
#include "z80sio.h"
#include "z80pio.h"
#include "z80ctc.h"
#include "z80dma.h"
#include "i8272.h"

#define CHIPS_UTIL_IMPL
#include "z80dasm.h"

#include "partner_crt.hpp"
#include "debugger.hpp"
#include "gui/gui.hpp"
#include "gui/display.hpp"
#include <iostream>
#include <string>
#include <cstring>

static constexpr uint32_t CPU_CLOCK_HZ = 4000000;
static constexpr uint32_t TARGET_FPS = 60;
static constexpr uint32_t TICKS_PER_FRAME = CPU_CLOCK_HZ / TARGET_FPS;

void print_usage(const char *prog)
{
    std::cerr << "Usage: " << prog << " [rom_file] [options]\n";
    std::cerr << "Options:\n";
    std::cerr << "  --help           Show this help\n";
    std::cerr << "  --terminal TYPE  Terminal profile: vt52|vt100\n";
    std::cerr << "Default ROM: roms/partner_crt.rom\n";
}

int main(int argc, char **argv)
{
    std::string rom_file = "roms/partner_crt.rom";
    bool rom_set_from_cli = false;
    terminal_profile term_profile = terminal_profile::vt52;

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--help") == 0)
        {
            print_usage(argv[0]);
            return 0;
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
        else if (!rom_set_from_cli)
        {
            rom_file = argv[i];
            rom_set_from_cli = true;
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
        idp.load_rom(rom_file);
        idp.reset();

        gui app_gui;
        if (!app_gui.init("Iskra Delta Partner Emulator", 1280, 800))
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
                // Free-run mode: execute one frame's worth of ticks
                for (uint32_t i = 0; i < TICKS_PER_FRAME; i++)
                {
                    idp.tick();
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
