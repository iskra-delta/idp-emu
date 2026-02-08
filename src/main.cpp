// main.cpp - Partner emulator entry point
#define CHIPS_IMPL
#include "z80.h"
#include "z80sio.h"
#include "z80pio.h"
#include "z80ctc.h"
#include "z80dma.h"

#include "partner_crt.hpp"
#include <iostream>
#include <string>
#include <cstring>

void print_usage(const char *prog)
{
    std::cerr << "Usage: " << prog << " <rom_file> [options]\n";
    std::cerr << "Options:\n";
    std::cerr << "  --trace          Enable execution trace (registers)\n";
    std::cerr << "  --disasm         Enable disassembly output\n";
    std::cerr << "  --max-cycles N   Run for N cycles then exit\n";
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
    bool trace = false;
    bool disasm = false;
    uint64_t max_cycles = 0;

    // Parse arguments
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--trace") == 0)
        {
            trace = true;
        }
        else if (strcmp(argv[i], "--disasm") == 0)
        {
            disasm = true;
        }
        else if (strcmp(argv[i], "--max-cycles") == 0)
        {
            if (i + 1 < argc)
            {
                max_cycles = std::stoull(argv[++i]);
            }
            else
            {
                std::cerr << "Error: --max-cycles requires an argument\n";
                return 1;
            }
        }
        else if (strcmp(argv[i], "--help") == 0)
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
        idp.enable_trace(trace);
        idp.enable_disassembly(disasm);

        idp.load_rom(rom_file);
        idp.reset();

        std::cout << "[info] Starting emulation...\n";
        if (trace) std::cout << "[info] Trace enabled\n";
        if (disasm) std::cout << "[info] Disassembly enabled\n";
        if (max_cycles > 0) std::cout << "[info] Running for " << max_cycles << " cycles\n";

        uint64_t cycles = 0;
        while (max_cycles == 0 || cycles < max_cycles)
        {
            idp.tick();
            cycles++;
        }

        std::cout << "\n[info] Emulation stopped after " << cycles << " cycles\n";
    }
    catch (const std::exception &e)
    {
        std::cerr << "[error] " << e.what() << "\n";
        return 1;
    }

    return 0;
}