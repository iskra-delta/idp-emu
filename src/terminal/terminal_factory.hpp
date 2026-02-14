#pragma once
#include <memory>

class terminal_emulator;

enum class terminal_profile
{
    vt52,
    vt100_ansi
};

std::unique_ptr<terminal_emulator> make_terminal_emulator(terminal_profile profile);
