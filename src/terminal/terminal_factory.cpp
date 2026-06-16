#include "terminal_factory.hpp"
#include "terminal_emulator.hpp"
#include "vt52_terminal.hpp"

std::unique_ptr<terminal_emulator> make_terminal_emulator(terminal_profile profile)
{
    switch (profile)
    {
    case terminal_profile::vt52:
        return std::make_unique<vt52_terminal>(true);
    case terminal_profile::vt100_ansi:
        // Shared terminal currently implements VT52 plus a small ANSI subset
        // (cursor visibility, clear/home, cursor motion, SGR highlight/inverse).
        return std::make_unique<vt52_terminal>(true);
    default:
        return std::make_unique<vt52_terminal>(true);
    }
}
