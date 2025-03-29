/*#
    # z80sio.h

    Header-only emulator for the Zilog Z80 SIO (Serial Input/Output)
    written in C.

    Do this:
    ~~~C
    #define CHIPS_IMPL
    ~~~
    before you include this file in *one* C or C++ file to create the
    implementation.

    Optionally provide the following macros with your own implementation:
    ~~~C
    CHIPS_ASSERT(c)
    ~~~
        your own assert macro (default: assert(c))

    ## Emulated Pins:

    *********************************************
    *           +-----------+                   *
    *   D0 <--> |           | <--> TXA/RXA      *
    *   .. <--> |           | <--> TXB/RXB      *
    *   D7 <--> |           |                   *
    *   CE ---> |           |                   *
    * CS_A ---> |           |                   *
    * CS_B ---> |    Z80    | <--- WR           *
    * IORQ ---> |    SIO    | <--- RD           *
    *   M1 ---> |           | <--- RESET        *
    *  INT <--- |           |                   *
    *  IEI ---> |           | <--- RETI         *
    *  IEO <--- |           |                   *
    *           +-----------+                   *
    *********************************************

    ## Supported Features:

    - Dual asynchronous serial channels (A and B)
    - TX and RX data handling
    - Basic control register write (WR0–WR7)
    - External interrupt daisy chaining
    - Software vector assignment (WR2)
    - Simple pin-based tick loop integration

    ## Not Implemented (yet):

    - Full interrupt mode detection (parity, framing, error)
    - Sync/SDLC/HDLC modes
    - Baud rate generation (requires CTC)
    - Modem control signals (CTS, RTS, DCD)
    - FIFO buffering
    - Control/status register reads (RRx)

    ## Usage:

    Initialize:

    ~~~C
    z80sio_t sio;
    z80sio_init(&sio);
    ~~~

    On each CPU tick:

    ~~~C
    pins = z80sio_tick(&sio, pins);
    ~~~

    To send or receive data:
    - Write to the data register (TX)
    - Inject data into RX buffer manually
    - Read from RX register on read cycle

    ## License:

    zlib/libpng license

    Copyright (c) 2025 GPT

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

#ifdef __cplusplus
extern "C"
{
#endif

#define Z80SIO_NUM_CHANNELS (2)

#define Z80SIO_CHANNEL_A (0)
#define Z80SIO_CHANNEL_B (1)

/* Pin Assignments */
#define Z80SIO_PIN_M1 (24)
#define Z80SIO_PIN_IORQ (26)
#define Z80SIO_PIN_RD (27)
#define Z80SIO_PIN_WR (28)
#define Z80SIO_PIN_INT (30)
#define Z80SIO_PIN_RESET (31)
#define Z80SIO_PIN_IEIO (37)
#define Z80SIO_PIN_RETI (38)
#define Z80SIO_PIN_CE (40)
#define Z80SIO_PIN_CS_A (41) // Select channel A
#define Z80SIO_PIN_CS_B (42) // Select channel B

#define Z80SIO_M1 (1ULL << Z80SIO_PIN_M1)
#define Z80SIO_IORQ (1ULL << Z80SIO_PIN_IORQ)
#define Z80SIO_RD (1ULL << Z80SIO_PIN_RD)
#define Z80SIO_WR (1ULL << Z80SIO_PIN_WR)
#define Z80SIO_INT (1ULL << Z80SIO_PIN_INT)
#define Z80SIO_RESET (1ULL << Z80SIO_PIN_RESET)
#define Z80SIO_IEIO (1ULL << Z80SIO_PIN_IEIO)
#define Z80SIO_RETI (1ULL << Z80SIO_PIN_RETI)
#define Z80SIO_CE (1ULL << Z80SIO_PIN_CE)
#define Z80SIO_CS_A (1ULL << Z80SIO_PIN_CS_A)
#define Z80SIO_CS_B (1ULL << Z80SIO_PIN_CS_B)

#define Z80SIO_GET_DATA(p) ((uint8_t)(((p) >> 16) & 0xFF))
#define Z80SIO_SET_DATA(p, d)                                   \
    {                                                           \
        p = ((p) & ~0xFF0000ULL) | (((d) << 16) & 0xFF0000ULL); \
    }

    /* SIO Channel */
    typedef struct
    {
        uint8_t rx_data;
        uint8_t tx_data;
        bool rx_ready;
        bool tx_ready;
        uint8_t control[8];
        uint8_t control_index;
        uint8_t int_vector;
        uint8_t int_state;
        bool expect_vector;
    } z80sio_channel_t;

    /* SIO device */
    typedef struct
    {
        z80sio_channel_t chn[Z80SIO_NUM_CHANNELS];
        uint64_t pins;
    } z80sio_t;

/* Interrupt states */
#define Z80SIO_INT_NEEDED (1 << 0)
#define Z80SIO_INT_REQUESTED (1 << 1)
#define Z80SIO_INT_SERVICED (1 << 2)

    void z80sio_init(z80sio_t *sio);
    void z80sio_reset(z80sio_t *sio);
    uint64_t z80sio_tick(z80sio_t *sio, uint64_t pins);

#ifdef __cplusplus
}
#endif

/*--- IMPLEMENTATION ---*/
#ifdef CHIPS_IMPL

void z80sio_init(z80sio_t *sio)
{
    memset(sio, 0, sizeof(z80sio_t));
    z80sio_reset(sio);
}

void z80sio_reset(z80sio_t *sio)
{
    for (int i = 0; i < Z80SIO_NUM_CHANNELS; i++)
    {
        z80sio_channel_t *ch = &sio->chn[i];
        ch->rx_ready = false;
        ch->tx_ready = true;
        ch->int_state = 0;
        ch->control_index = 0;
        ch->expect_vector = false;
    }
}

static inline int _z80sio_select_channel(uint64_t pins)
{
    return (pins & Z80SIO_CS_B) ? Z80SIO_CHANNEL_B : Z80SIO_CHANNEL_A;
}

static inline void _z80sio_write_control(z80sio_channel_t *ch, uint8_t data)
{
    if (ch->expect_vector)
    {
        ch->int_vector = data & 0xF8;
        ch->expect_vector = false;
    }
    else
    {
        uint8_t reg = data & 0x07;
        ch->control[reg] = data;
        if (reg == 2)
        {
            ch->int_vector = data & 0xF8;
        }
        if (reg == 0)
        {
            ch->expect_vector = true;
        }
    }
}

static inline uint64_t _z80sio_io(z80sio_t *sio, uint64_t pins)
{
    int chn_id = _z80sio_select_channel(pins);
    z80sio_channel_t *ch = &sio->chn[chn_id];

    if (pins & Z80SIO_RD)
    {
        Z80SIO_SET_DATA(pins, ch->rx_data);
        ch->rx_ready = false;
    }
    else if (pins & Z80SIO_WR)
    {
        uint8_t data = Z80SIO_GET_DATA(pins);
        if (pins & Z80SIO_CS_A)
        {
            _z80sio_write_control(ch, data);
        }
        else
        {
            ch->tx_data = data;
            ch->tx_ready = false;
        }
    }
    return pins;
}

static inline uint64_t _z80sio_int(z80sio_t *sio, uint64_t pins)
{
    for (int i = 0; i < Z80SIO_NUM_CHANNELS; i++)
    {
        z80sio_channel_t *ch = &sio->chn[i];

        if ((pins & Z80SIO_RETI) && (ch->int_state & Z80SIO_INT_SERVICED))
        {
            ch->int_state &= ~Z80SIO_INT_SERVICED;
            pins &= ~Z80SIO_RETI;
        }

        if ((ch->int_state != 0) && (pins & Z80SIO_IEIO))
        {
            pins &= ~Z80SIO_IEIO;
            if (ch->int_state & Z80SIO_INT_NEEDED)
            {
                pins |= Z80SIO_INT;
                ch->int_state = (ch->int_state & ~Z80SIO_INT_NEEDED) | Z80SIO_INT_REQUESTED;
            }
            if ((ch->int_state & Z80SIO_INT_REQUESTED) && ((pins & (Z80SIO_IORQ | Z80SIO_M1)) == (Z80SIO_IORQ | Z80SIO_M1)))
            {
                Z80SIO_SET_DATA(pins, ch->int_vector);
                ch->int_state = (ch->int_state & ~Z80SIO_INT_REQUESTED) | Z80SIO_INT_SERVICED;
                pins &= ~Z80SIO_INT;
            }
        }
    }
    return pins;
}

uint64_t z80sio_tick(z80sio_t *sio, uint64_t pins)
{
    if ((pins & (Z80SIO_CE | Z80SIO_IORQ | Z80SIO_M1)) == (Z80SIO_CE | Z80SIO_IORQ))
    {
        pins = _z80sio_io(sio, pins);
    }
    pins = _z80sio_int(sio, pins);
    sio->pins = pins;
    return pins;
}
#endif // CHIPS_IMPL
