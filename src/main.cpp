// main.cpp - Partner emulator entry point
#define CHIPS_IMPL
#include "z80.h"
#include "z80sio.h"
#include "z80pio.h"
#include "z80ctc.h"
#include "z80dma.h"

#define CHIPS_UTIL_IMPL
#include "z80dasm.h"

#include "partner_crt.hpp"
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
    std::cerr << "Usage: " << prog << " <rom_file> [options]\n";
    std::cerr << "Options:\n";
    std::cerr << "  --help           Show this help\n";
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        print_usage(argv[0]);
        return 1;
    }

    std::string rom_file;

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--help") == 0)
        {
            print_usage(argv[0]);
            return 0;
        }
        else if (rom_file.empty())
        {
            rom_file = argv[i];
        }
        else
        {
            std::cerr << "Error: Unknown option: " << argv[i] << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    if (rom_file.empty())
    {
        std::cerr << "Error: No ROM file specified\n";
        print_usage(argv[0]);
        return 1;
    }

    try
    {
        partner_crt idp;
        idp.load_rom(rom_file);
        idp.reset();

        gui app_gui;
        if (!app_gui.init("Iskra Delta Partner Emulator", 1280, 800))
        {
            std::cerr << "[error] Failed to initialize GUI\n";
            return 1;
        }

        // Draw CP/M-style terminal text to test CRT display
        display &disp = app_gui.get_display();

        disp.draw_text(0, 0, "CP/M 2.2  ISKRA DELTA PARTNER");
        disp.draw_text(0, 1, "62K TPA");
        disp.draw_text(0, 3, "A>DIR");
        disp.draw_text(0, 4, "A: PARTNER  COM : BASIC    COM : TURBO    COM");
        disp.draw_text(0, 5, "A: ASM      COM : DDT      COM : PIP      COM");
        disp.draw_text(0, 6, "A: STAT     COM : ED       COM : SUBMIT   COM");
        disp.draw_text(0, 7, "A: LOAD     COM : DUMP     COM : XSUB     COM");
        disp.draw_text(0, 9, "A>STAT *.*");
        disp.draw_text(0, 10, " Recs  Bytes  Ext Acc");
        disp.draw_text(0, 11, "   48     8k    1 R/W A:PARTNER.COM");
        disp.draw_text(0, 12, "   24     4k    1 R/W A:BASIC.COM");
        disp.draw_text(0, 13, "   96    12k    1 R/W A:TURBO.COM");
        disp.draw_text(0, 14, "   16     2k    1 R/W A:ASM.COM");
        disp.draw_text(0, 15, "   20     4k    1 R/W A:DDT.COM");
        disp.draw_text(0, 16, "   58     8k    1 R/W A:PIP.COM");
        disp.draw_text(0, 17, "   44     6k    1 R/W A:STAT.COM");
        disp.draw_text(0, 18, "   52     8k    1 R/W A:ED.COM");
        disp.draw_text(0, 19, "   28     4k    1 R/W A:SUBMIT.COM");
        disp.draw_text(0, 21, "A>_");

        bool running = true;
        bool paused = true;

        std::cout << "[info] Starting emulation (paused)...\n";
        std::cout << "[info] Press SPACE to run/pause, ESC to quit\n";

        while (running)
        {
            running = app_gui.process_events(paused);

            if (!paused)
            {
                for (uint32_t i = 0; i < TICKS_PER_FRAME; i++)
                {
                    idp.tick();
                }
            }

            app_gui.begin_frame();
            app_gui.render_panels(idp, paused);
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
