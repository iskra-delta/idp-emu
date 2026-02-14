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
        // Placeholder strategy: route to VT52 until VT100/ANSI implementation is added.
        return std::make_unique<vt52_terminal>(true);
    default:
        return std::make_unique<vt52_terminal>(true);
    }
}
