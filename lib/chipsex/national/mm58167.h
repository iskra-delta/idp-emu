#pragma once
/*#
    # mm58167a.h

    Header-only emulator for the National Semiconductor MM58167A
    Real-Time Clock (RTC) chip.

    Do this:
    ~~~C
    #define CHIPS_IMPL
    ~~~
    before including this file in *one* C or C++ file to create the
    implementation.

    Optionally provide your own assert macro:
    ~~~C
    #define CHIPS_ASSERT(x)
    ~~~

    ## Emulated Pins

    The MM58167A is a memory-mapped clock/calendar device that provides
    Binary-Coded Decimal (BCD) time and date registers.

    ***************************************
    *           +-----------+             *
    * D0..D7 <->|           |<-> A0..A3   *
    *           |           |             *
    *   CS̅  --->|           |             *
    *   AS̅  --->|           |             *
    *   DS̅  --->| MM58167A  |---> INT     *
    *   RW  --->|           |---> 1HZ     *
    *   ET̅  --->|           |             *
    *           +-----------+             *
    ***************************************

    - D0..D7: Bidirectional 8-bit data bus
    - A0..A3: Latched register address
    - CS̅:    Chip Select (active-low)
    - AS̅:    Address Strobe (latches A0..A3)
    - DS̅:    Data Strobe (active-low)
    - RW:     Read (1) / Write (0) select
    - RESET̅: Reset (active-low)
    - INT:    (TODO) interrupt output
    - 1HZ:    (TODO) 1 Hz pulse output

    ## Functions

    ~~~C
    void mm58167a_init(mm58167a_t* chip);
    ~~~
        Initializes the MM58167A instance and syncs current time.

    ~~~C
    void mm58167a_reset(mm58167a_t* chip);
    ~~~
        Clears registers and resets internal state.

    ~~~C
    uint64_t mm58167a_tick(mm58167a_t* chip, uint64_t pins);
    ~~~
        Tick the chip once, handling latches, reads, and writes based on
        control pins. Returns possibly modified pin mask (e.g. for data output).

    ~~~C
    void mm58167a_sync_time(mm58167a_t* chip);
    ~~~
        Manually sync registers with the host system clock.

    ## HOWTO

    Use `mm58167a_tick()` once per system tick. Provide `pins` containing
    the data bus, control lines, and optionally the address on A0..A3.

    Example usage:
    ~~~C
    mm58167a_t rtc;
    mm58167a_init(&rtc);

    // simulate: latch address 0x02 (hours)
    uint64_t pins = 0;
    pins |= MM58167A_CS | MM58167A_DS | MM58167A_RW;
    pins &= ~MM58167A_AS;
    pins |= 0x02;  // A0..A3 on bits 0..3
    pins = mm58167a_tick(&rtc, pins);

    // now perform read cycle
    pins &= ~MM58167A_DS;
    pins = mm58167a_tick(&rtc, pins);
    uint8_t hour_bcd = MM58167A_GET_DATA(pins);
    ~~~

    To simulate time passing, call `mm58167a_sync_time()` periodically
    or tick a 1 Hz signal through your CTC or system timer.

    ### Notes

    - Registers are always BCD encoded
    - Time is latched on every access and reflects host `time()` value
    - INT and 1HZ lines are planned but not implemented
    - Writes update register content, but won't alter real time

    You can use the MM58167A alongside a Z80 and PIO/CTC for real-time clock
    and timed interrupt simulation.

    ## License

    zlib/libpng license

    Copyright (c) 2025

    This software is provided 'as-is', without any express or implied warranty.
    In no event will the authors be held liable for any damages arising from
    the use of this software. Permission is granted to anyone to use this software
    for any purpose, including commercial applications, and to alter it and redistribute
    it freely, subject to the following restrictions:

        1. The origin of this software must not be misrepresented; you must not
           claim that you wrote the original software. If you use this software in a
           product, an acknowledgment in the product documentation would be
           appreciated but is not required.
        2. Altered source versions must be plainly marked as such, and must not
           be misrepresented as being the original software.
        3. This notice may not be removed or altered from any source distribution.
#*/

#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>

#ifdef __cplusplus
extern "C"
{
#endif

// CHIPS-style pin assignments
#define MM58167A_PIN_CS (40)
#define MM58167A_PIN_AS (41)
#define MM58167A_PIN_DS (42)
#define MM58167A_PIN_RW (43)
#define MM58167A_PIN_RESET (44)
#define MM58167A_PIN_INT (45) // output (TODO)
#define MM58167A_PIN_1HZ (46) // output (TODO)

#define MM58167A_CS (1ULL << MM58167A_PIN_CS)
#define MM58167A_AS (1ULL << MM58167A_PIN_AS)
#define MM58167A_DS (1ULL << MM58167A_PIN_DS)
#define MM58167A_RW (1ULL << MM58167A_PIN_RW)
#define MM58167A_RESET (1ULL << MM58167A_PIN_RESET)
#define MM58167A_INT (1ULL << MM58167A_PIN_INT)
#define MM58167A_1HZ (1ULL << MM58167A_PIN_1HZ)

#define MM58167A_GET_DATA(p) ((uint8_t)(((p) >> 16) & 0xFF))
#define MM58167A_SET_DATA(p, d)                                           \
    {                                                                     \
        p = ((p) & ~0xFF0000ULL) | (((uint64_t)(d) << 16) & 0xFF0000ULL); \
    }

    // RTC state
    typedef struct
    {
        uint8_t regs[32];      // MM58167 has 32 registers
        uint8_t addr_latch;    // latched address (via AS)
        time_t last_sync_time; // for refreshing time
    } mm58167a_t;

    void mm58167a_init(mm58167a_t *chip);
    void mm58167a_reset(mm58167a_t *chip);
    uint64_t mm58167a_tick(mm58167a_t *chip, uint64_t pins);
    void mm58167a_sync_time(mm58167a_t *chip);

#ifdef __cplusplus
}
#endif

#ifdef CHIPS_IMPL
void mm58167a_init(mm58167a_t *chip)
{
    memset(chip, 0, sizeof(mm58167a_t));
    mm58167a_reset(chip);
}

void mm58167a_reset(mm58167a_t *chip)
{
    memset(chip->regs, 0, sizeof(chip->regs));
    chip->addr_latch = 0;
    chip->last_sync_time = 0;
    mm58167a_sync_time(chip);
}

void mm58167a_sync_time(mm58167a_t *chip)
{
    time_t now = time(NULL);
    if (now == chip->last_sync_time)
        return;
    chip->last_sync_time = now;

    struct tm *t = localtime(&now);

    chip->regs[0x00] = ((t->tm_sec % 10) | ((t->tm_sec / 10) << 4));           // sec
    chip->regs[0x01] = ((t->tm_min % 10) | ((t->tm_min / 10) << 4));           // min
    chip->regs[0x02] = ((t->tm_hour % 10) | ((t->tm_hour / 10) << 4));         // hour
    chip->regs[0x03] = (t->tm_wday & 0x07);                                    // day of week
    chip->regs[0x04] = ((t->tm_mday % 10) | ((t->tm_mday / 10) << 4));         // day
    chip->regs[0x05] = ((t->tm_mon + 1) % 10) | (((t->tm_mon + 1) / 10) << 4); // month
    chip->regs[0x06] = ((t->tm_year % 10) | (((t->tm_year / 10) % 10) << 4));  // year
}

uint64_t mm58167a_tick(mm58167a_t *chip, uint64_t pins)
{
    if ((pins & MM58167A_RESET) == 0)
    {
        mm58167a_reset(chip);
        return pins;
    }

    mm58167a_sync_time(chip);

    // latch address
    if ((pins & MM58167A_AS) == 0)
    {
        chip->addr_latch = (uint8_t)(pins & 0x0F); // lower address lines A0–A3
    }

    if ((pins & MM58167A_CS) == 0 && (pins & MM58167A_DS) == 0)
    {
        if (pins & MM58167A_RW)
        {
            // read
            uint8_t data = chip->regs[chip->addr_latch & 0x1F];
            MM58167A_SET_DATA(pins, data);
        }
        else
        {
            // write
            uint8_t data = MM58167A_GET_DATA(pins);
            chip->regs[chip->addr_latch & 0x1F] = data;
        }
    }

    // TODO: Handle INT and 1HZ generation
    return pins;
}
#endif // CHIPS_IMPL
