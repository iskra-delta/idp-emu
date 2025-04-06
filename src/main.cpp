// chips_impl.cpp
#define CHIPS_IMPL
#include "z80.h"
#include "z80sio.h"

#include "partner_crt.hpp"
#include <iostream>

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        std::cerr << "usage: idp-emu <rom_file>\n";
        return 1;
    }

    try
    {
        partner_crt idp;
        idp.load_rom(argv[1]);
        idp.reset();

        while (true)
        {
            idp.tick();
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "[error] " << e.what() << "\n";
        return 1;
    }
}
