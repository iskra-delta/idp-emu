#pragma once
#include <cstdint>

class display;

class terminal_emulator
{
public:
    virtual ~terminal_emulator() = default;
    virtual void reset() = 0;
    virtual void put_char(uint8_t ch) = 0;
    virtual void render_to(display &disp) const = 0;
};
