#pragma once
#include <cstdint>

enum class dbg_action {
    NONE,
    STEP_INTO,
    STEP_OVER,
    SWITCH_TO_GDP
};

// Returns true if opcode is a CALL or RST instruction (subroutine call)
inline bool is_call_or_rst(uint8_t opcode)
{
    // RST instructions: 0xC7, 0xCF, 0xD7, 0xDF, 0xE7, 0xEF, 0xF7, 0xFF
    if ((opcode & 0xC7) == 0xC7)
        return true;
    // CALL cc,nn: 0xC4, 0xCC, 0xD4, 0xDC, 0xE4, 0xEC, 0xF4, 0xFC
    if ((opcode & 0xC7) == 0xC4)
        return true;
    // CALL nn: 0xCD
    if (opcode == 0xCD)
        return true;
    return false;
}
