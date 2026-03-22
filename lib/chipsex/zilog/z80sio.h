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

    - Sync/SDLC/HDLC modes
    - Baud rate generation (requires external clock/CTC)
    - Hardware FIFO buffering (software single-byte buffering provided)
    - Full modem control timing (CTS, RTS, DCD pins functional but simplified timing)

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
#define Z80SIO_PIN_CS_A (41) // Select channel A (B/A on real chip)
#define Z80SIO_PIN_CS_B (42) // Select channel B (C/D on real chip)

/* Modem control pins for channel A */
#define Z80SIO_PIN_DCDA (43)  // Data Carrier Detect A (input)
#define Z80SIO_PIN_CTSA (44)  // Clear To Send A (input)
#define Z80SIO_PIN_RTSA (45)  // Request To Send A (output)
#define Z80SIO_PIN_DTRA (46)  // Data Terminal Ready A (output)

/* Modem control pins for channel B */
#define Z80SIO_PIN_DCDB (47)  // Data Carrier Detect B (input)
#define Z80SIO_PIN_CTSB (48)  // Clear To Send B (input)
#define Z80SIO_PIN_RTSB (49)  // Request To Send B (output)
#define Z80SIO_PIN_DTRB (50)  // Data Terminal Ready B (output)

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

#define Z80SIO_DCDA (1ULL << Z80SIO_PIN_DCDA)
#define Z80SIO_CTSA (1ULL << Z80SIO_PIN_CTSA)
#define Z80SIO_RTSA (1ULL << Z80SIO_PIN_RTSA)
#define Z80SIO_DTRA (1ULL << Z80SIO_PIN_DTRA)
#define Z80SIO_DCDB (1ULL << Z80SIO_PIN_DCDB)
#define Z80SIO_CTSB (1ULL << Z80SIO_PIN_CTSB)
#define Z80SIO_RTSB (1ULL << Z80SIO_PIN_RTSB)
#define Z80SIO_DTRB (1ULL << Z80SIO_PIN_DTRB)

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
        bool rx_ready;          // RX character available
        bool tx_ready;          // TX buffer empty
        uint8_t wr[8];          // Write registers WR0-WR7
        uint8_t rr[3];          // Read registers RR0-RR2 (cached status)
        uint8_t reg_index;      // Currently selected register (from WR0)
        uint8_t int_vector;     // Interrupt vector
        uint8_t int_state;      // Interrupt state machine

        // Status flags for RR0
        bool dcd;               // Data Carrier Detect
        bool cts;               // Clear To Send
        bool tx_underrun;       // TX underrun/EOM
        bool break_abort;       // Break/Abort detected
        bool sync_hunt;         // Sync/Hunt status (RR0 D4)

        // Status flags for RR1
        bool parity_error;      // Parity error
        bool rx_overrun;        // RX overrun error
        bool framing_error;     // Framing error
        bool end_of_frame;      // End of frame (SDLC)
        uint8_t residue_code;   // RR1 D1..D3

        // Control state
        bool rts;               // Request To Send (output)
        bool dtr;               // Data Terminal Ready (output)
        bool rx_int_on_first_armed; // WR1 mode 1 state
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

    // Helper functions to inject received data from external source
    void z80sio_rx_data(z80sio_t *sio, int channel, uint8_t data);

    // Helper function to check if TX is ready
    bool z80sio_tx_ready(z80sio_t *sio, int channel);

    // Helper function to get TX data
    uint8_t z80sio_tx_data(z80sio_t *sio, int channel);

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
        // Clear data registers
        ch->rx_data = 0;
        ch->tx_data = 0;
        ch->rx_ready = false;
        ch->tx_ready = true;

        // Clear write registers
        for (int j = 0; j < 8; j++) {
            ch->wr[j] = 0;
        }

        // Clear read registers
        for (int j = 0; j < 3; j++) {
            ch->rr[j] = 0;
        }

        // Reset state
        ch->reg_index = 0;
        ch->int_vector = 0;
        ch->int_state = 0;

        // Clear status flags
        ch->dcd = false;
        ch->cts = false;
        ch->tx_underrun = true; // RR0 D6 is set after reset
        ch->break_abort = false;
        ch->sync_hunt = false;
        ch->parity_error = false;
        ch->rx_overrun = false;
        ch->framing_error = false;
        ch->end_of_frame = false;
        ch->residue_code = 0;

        // Reset control outputs
        ch->rts = false;
        ch->dtr = false;
        ch->rx_int_on_first_armed = true;
    }
}

static inline int _z80sio_select_channel(uint64_t pins)
{
    return (pins & Z80SIO_CS_B) ? Z80SIO_CHANNEL_B : Z80SIO_CHANNEL_A;
}

static inline bool _z80sio_odd_parity_u8(uint8_t v)
{
    v ^= (uint8_t)(v >> 4);
    v ^= (uint8_t)(v >> 2);
    v ^= (uint8_t)(v >> 1);
    return (v & 1u) != 0;
}

static inline uint8_t _z80sio_rx_bits_per_char(const z80sio_channel_t *ch)
{
    switch ((ch->wr[3] >> 6) & 0x03u)
    {
    case 0: return 5;
    case 1: return 7;
    case 2: return 6;
    default: return 8;
    }
}

static inline uint8_t _z80sio_rx_int_mode(const z80sio_channel_t *ch)
{
    return (uint8_t)((ch->wr[1] >> 3) & 0x03u);
}

static inline bool _z80sio_parity_is_special(const z80sio_channel_t *ch)
{
    return _z80sio_rx_int_mode(ch) == 2;
}

static inline bool _z80sio_special_rx_condition(const z80sio_channel_t *ch)
{
    return ch->rx_overrun || ch->framing_error || ch->end_of_frame ||
        (_z80sio_parity_is_special(ch) && ch->parity_error);
}

static inline bool _z80sio_special_rx_condition_no_parity(const z80sio_channel_t *ch)
{
    return ch->rx_overrun || ch->framing_error || ch->end_of_frame;
}

static inline void _z80sio_maybe_raise_rx_interrupt(z80sio_channel_t *ch, bool special_without_parity)
{
    switch (_z80sio_rx_int_mode(ch))
    {
    case 0:
        // RX interrupts disabled.
        break;
    case 1:
        // Interrupt on first character only; after the first, only special RX
        // conditions (except parity) may interrupt until re-armed.
        if (ch->rx_int_on_first_armed)
        {
            ch->int_state |= Z80SIO_INT_NEEDED;
            ch->rx_int_on_first_armed = false;
        }
        else if (special_without_parity)
        {
            ch->int_state |= Z80SIO_INT_NEEDED;
        }
        break;
    case 2:
    case 3:
        ch->int_state |= Z80SIO_INT_NEEDED;
        break;
    default:
        break;
    }
}

/* Update RR0 status register */
static inline void _z80sio_update_rr0(z80sio_channel_t *ch, uint64_t pins, int chn_id)
{
    const bool dcd_pin = (chn_id == Z80SIO_CHANNEL_A) ?
        ((pins & Z80SIO_DCDA) != 0) : ((pins & Z80SIO_DCDB) != 0);
    const bool cts_pin = (chn_id == Z80SIO_CHANNEL_A) ?
        ((pins & Z80SIO_CTSA) != 0) : ((pins & Z80SIO_CTSB) != 0);
    ch->dcd = dcd_pin;
    ch->cts = cts_pin;

    ch->rr[0] = 0;
    if (ch->rx_ready) ch->rr[0] |= (1 << 0);        // RX character available
    if ((chn_id == Z80SIO_CHANNEL_A) && ch->int_state)
        ch->rr[0] |= (1 << 1);                       // Interrupt pending (Ch A only)
    if (ch->tx_ready) ch->rr[0] |= (1 << 2);        // TX buffer empty
    if (ch->dcd) ch->rr[0] |= (1 << 3);             // DCD
    if (ch->sync_hunt) ch->rr[0] |= (1 << 4);       // Sync/Hunt
    if (ch->cts) ch->rr[0] |= (1 << 5);             // CTS
    if (ch->tx_underrun) ch->rr[0] |= (1 << 6);     // TX underrun/EOM
    if (ch->break_abort) ch->rr[0] |= (1 << 7);     // Break/Abort
}

/* Update RR1 status register */
static inline void _z80sio_update_rr1(z80sio_channel_t *ch)
{
    ch->rr[1] = 0;
    if (ch->tx_ready) ch->rr[1] |= (1 << 0);        // All sent (approximation)
    ch->rr[1] |= (uint8_t)((ch->residue_code & 0x07u) << 1); // SDLC residue
    if (ch->parity_error) ch->rr[1] |= (1 << 4);    // Parity error
    if (ch->rx_overrun) ch->rr[1] |= (1 << 5);      // RX overrun error
    if (ch->framing_error) ch->rr[1] |= (1 << 6);   // CRC/Framing error
    if (ch->end_of_frame) ch->rr[1] |= (1 << 7);    // End of frame
}

/* Write to control register */
static inline void _z80sio_write_control(z80sio_t *sio, int chn_id, uint8_t data)
{
    z80sio_channel_t *ch = &sio->chn[chn_id];

    if (ch->reg_index == 0)
    {
        ch->wr[0] = data;
        const uint8_t ptr = data & 0x07u;
        const uint8_t cmd = (uint8_t)((data >> 3) & 0x07u);
        const uint8_t crc_reset_code = (uint8_t)((data >> 6) & 0x03u);

        switch (cmd)
        {
        case 0: // Null command
            break;
        case 1: // Send abort (SDLC)
            ch->break_abort = true;
            if (ch->wr[1] & (1u << 0))
                ch->int_state |= Z80SIO_INT_NEEDED;
            break;
        case 2: // Reset External/Status interrupts
            // Re-arm External/Status detection; keep line-status bits intact.
            break;
        case 3: // Channel reset
            {
                z80sio_channel_t keep_vec = *ch;
                memset(ch, 0, sizeof(*ch));
                ch->tx_ready = true;
                ch->tx_underrun = true;
                ch->sync_hunt = false;
                ch->rx_int_on_first_armed = true;
                if (chn_id == Z80SIO_CHANNEL_B)
                    ch->int_vector = keep_vec.int_vector;
                else
                    ch->int_vector = sio->chn[Z80SIO_CHANNEL_B].int_vector;
            }
            break;
        case 4: // Enable interrupt on next receive character
            ch->rx_int_on_first_armed = true;
            break;
        case 5: // Reset TX interrupt pending
            // Keep any receive-driven request active.
            if (!ch->rx_ready && !_z80sio_special_rx_condition(ch))
                ch->int_state &= (uint8_t)~(Z80SIO_INT_NEEDED | Z80SIO_INT_REQUESTED);
            break;
        case 6: // Error reset (latches)
            ch->parity_error = false;
            ch->rx_overrun = false;
            ch->framing_error = false;
            ch->end_of_frame = false;
            ch->residue_code = 0;
            break;
        case 7: // Return from interrupt (Channel A command path)
            if (chn_id == Z80SIO_CHANNEL_A)
            {
                if (sio->chn[Z80SIO_CHANNEL_A].int_state & Z80SIO_INT_SERVICED)
                    sio->chn[Z80SIO_CHANNEL_A].int_state &= (uint8_t)~Z80SIO_INT_SERVICED;
                else if (sio->chn[Z80SIO_CHANNEL_B].int_state & Z80SIO_INT_SERVICED)
                    sio->chn[Z80SIO_CHANNEL_B].int_state &= (uint8_t)~Z80SIO_INT_SERVICED;
            }
            break;
        }

        // CRC reset command bits are shared with WR0 and always decoded.
        switch (crc_reset_code)
        {
        case 1: // Reset receive CRC checker
            ch->framing_error = false;
            ch->end_of_frame = false;
            break;
        case 2: // Reset transmit CRC generator
            break;
        case 3: // Reset TX underrun/EOM latch
            ch->tx_underrun = false;
            break;
        default:
            break;
        }

        ch->reg_index = ptr;
    }
    else
    {
        // Write to selected register
        uint8_t reg = ch->reg_index;
        ch->wr[reg] = data;
        ch->reg_index = 0; // Auto-reset to WR0

        switch (reg)
        {
        case 1: // WR1: Transmit/receive interrupt and data transfer mode
            if (_z80sio_rx_int_mode(ch) == 1)
                ch->rx_int_on_first_armed = true;
            break;

        case 2: // WR2: Interrupt vector (only in channel B)
            if (chn_id == Z80SIO_CHANNEL_B)
            {
                ch->int_vector = data;
                // Channel A also gets the base vector
                sio->chn[Z80SIO_CHANNEL_A].int_vector = data;
            }
            break;

        case 3: // WR3: Receive parameters and control
            if (data & (1u << 4))
                ch->sync_hunt = true;
            break;

        case 4: // WR4: Transmit/receive miscellaneous parameters and modes
            // Bits 0-1: Parity enable/odd-even
            // Bits 2-3: Stop bits (00=sync, 01=1 stop, 10=1.5 stop, 11=2 stop)
            // Bits 4-5: Sync mode
            // Bits 6-7: Clock rate (00=x1, 01=x16, 10=x32, 11=x64)
            break;

        case 5: // WR5: Transmit parameters and controls
            // Bit 0: TX CRC enable
            // Bit 1: RTS
            ch->rts = (data & (1 << 1)) != 0;
            // Bit 2: CRC-16/SDLC polynomial select
            // Bit 3: TX enable
            // Bit 4: Send break
            ch->break_abort = (data & (1u << 4)) != 0;
            // Bits 5-6: TX bits/character
            // Bit 7: DTR
            ch->dtr = (data & (1 << 7)) != 0;
            break;

        case 6: // WR6: Sync characters or SDLC address field
            break;

        case 7: // WR7: Sync character or SDLC flag
            break;
        }
    }
}

/* Read from control/data register */
static inline uint8_t _z80sio_read(z80sio_t *sio, int chn_id, bool control, uint64_t pins)
{
    z80sio_channel_t *ch = &sio->chn[chn_id];

    if (control)
    {
        // Read from control (status) register
        // Reading RR0-RR2 based on reg_index
        _z80sio_update_rr0(ch, pins, chn_id);
        _z80sio_update_rr1(ch);

        uint8_t reg = ch->reg_index;
        if (reg > 2) reg = 0; // Only RR0-RR2 exist

        uint8_t data = ch->rr[reg];
        ch->reg_index = 0; // Auto-reset to RR0
        return data;
    }
    else
    {
        // Read from data register
        ch->rx_ready = false;
        // Consuming the RX byte clears the pending RX cause. If the interrupt
        // hasn't been acknowledged yet, drop the outstanding request as well.
        if (ch->int_state & Z80SIO_INT_SERVICED) {
            ch->int_state &= ~Z80SIO_INT_NEEDED;
        } else {
            ch->int_state &= (uint8_t)~(Z80SIO_INT_NEEDED | Z80SIO_INT_REQUESTED);
        }
        return ch->rx_data;
    }
}

/* Write to control/data register */
static inline void _z80sio_write(z80sio_t *sio, int chn_id, bool control, uint8_t data)
{
    z80sio_channel_t *ch = &sio->chn[chn_id];

    if (control)
    {
        _z80sio_write_control(sio, chn_id, data);
    }
    else
    {
        // Write to data register
        ch->tx_data = data;
        ch->tx_ready = false;
        ch->tx_underrun = false;
    }
}

/* Handle I/O operations */
static inline uint64_t _z80sio_io(z80sio_t *sio, uint64_t pins)
{
    int chn_id = _z80sio_select_channel(pins);
    bool control = (pins & Z80SIO_CS_A) != 0; // CS_A selects control, CS_B selects data (inverted logic for channel selection)

    // Actually the channel selection is on B/A pin and C/D pin is control/data
    // Let me fix this logic - need to check the actual chip pinout
    // For now: CS_B set = channel B, CS_A set = control register

    if (pins & Z80SIO_RD)
    {
        uint8_t data = _z80sio_read(sio, chn_id, control, pins);
        Z80SIO_SET_DATA(pins, data);
    }
    else if (pins & Z80SIO_WR)
    {
        uint8_t data = Z80SIO_GET_DATA(pins);
        _z80sio_write(sio, chn_id, control, data);
    }
    return pins;
}

/* Get modified interrupt vector based on status (WR2 status affects vector) */
static inline uint8_t _z80sio_get_int_vector(z80sio_t *sio, int chn_id)
{
    z80sio_channel_t *ch = &sio->chn[chn_id];
    const uint8_t base_vector = sio->chn[Z80SIO_CHANNEL_B].int_vector;

    // Check if status affects vector (WR1 bit 2 in channel B)
    const bool status_affects_vector = (sio->chn[Z80SIO_CHANNEL_B].wr[1] & (1 << 2)) != 0;

    if (!status_affects_vector)
    {
        return base_vector;
    }

    // Bits 1..3 are modified by channel/cause.
    uint8_t modified = base_vector & 0xF1u;

    // Channel bit (bit 3): 0=B, 1=A
    if (chn_id == Z80SIO_CHANNEL_A) {
        modified |= (1u << 3);
    }

    // Type bits (bits 1..2): 00=TX, 01=Ext/Status, 10=RX, 11=Special RX
    if (ch->rx_ready && _z80sio_special_rx_condition(ch))
    {
        modified |= (3u << 1);
    }
    else if (ch->rx_ready && (_z80sio_rx_int_mode(ch) != 0))
    {
        modified |= (2u << 1);
    }
    else if (ch->tx_ready && (ch->wr[1] & (1u << 1)))
    {
        modified |= (0u << 1);
    }
    else
    {
        modified |= (1u << 1);
    }

    return modified;
}

/* Interrupt daisy chain handling */
static inline uint64_t _z80sio_int(z80sio_t *sio, uint64_t pins)
{
    for (int i = 0; i < Z80SIO_NUM_CHANNELS; i++)
    {
        z80sio_channel_t *ch = &sio->chn[i];

        // Handle RETI
        if ((pins & Z80SIO_RETI) && (ch->int_state & Z80SIO_INT_SERVICED))
        {
            ch->int_state &= ~Z80SIO_INT_SERVICED;
            pins &= ~Z80SIO_RETI;
        }

        // Interrupt daisy chain protocol
        if ((ch->int_state != 0) && (pins & Z80SIO_IEIO))
        {
            // Block downstream interrupts
            pins &= ~Z80SIO_IEIO;

            // Keep INT asserted from the first pending tick until the CPU
            // acknowledges the request, matching the level-like daisy-chain
            // behavior expected by Partner firmware.
            if (ch->int_state & (Z80SIO_INT_NEEDED | Z80SIO_INT_REQUESTED))
            {
                pins |= Z80SIO_INT;
            }

            // Request interrupt
            if (ch->int_state & Z80SIO_INT_NEEDED)
            {
                ch->int_state = (ch->int_state & ~Z80SIO_INT_NEEDED) | Z80SIO_INT_REQUESTED;
            }

            // Acknowledge interrupt and put vector on bus
            if ((ch->int_state & Z80SIO_INT_REQUESTED) &&
                ((pins & (Z80SIO_IORQ | Z80SIO_M1)) == (Z80SIO_IORQ | Z80SIO_M1)))
            {
                uint8_t vector = _z80sio_get_int_vector(sio, i);
                Z80SIO_SET_DATA(pins, vector);
                ch->int_state = (ch->int_state & ~Z80SIO_INT_REQUESTED) | Z80SIO_INT_SERVICED;
                pins &= ~Z80SIO_INT;
            }
        }
    }
    return pins;
}

/* Update modem control signals */
static inline uint64_t _z80sio_modem_control(z80sio_t *sio, uint64_t pins)
{
    // Channel A
    z80sio_channel_t *cha = &sio->chn[Z80SIO_CHANNEL_A];

    // Read input modem signals
    bool old_dcd = cha->dcd;
    bool old_cts = cha->cts;
    cha->dcd = (pins & Z80SIO_DCDA) != 0;
    cha->cts = (pins & Z80SIO_CTSA) != 0;

    // Detect transitions for external/status interrupts (WR1 bit 0)
    if (cha->wr[1] & (1 << 0))
    {
        if (old_dcd != cha->dcd || old_cts != cha->cts)
        {
            cha->int_state |= Z80SIO_INT_NEEDED;
        }
    }

    // Set output modem signals
    if (cha->rts) {
        pins |= Z80SIO_RTSA;
    } else {
        pins &= ~Z80SIO_RTSA;
    }
    if (cha->dtr) {
        pins |= Z80SIO_DTRA;
    } else {
        pins &= ~Z80SIO_DTRA;
    }

    // Channel B
    z80sio_channel_t *chb = &sio->chn[Z80SIO_CHANNEL_B];

    old_dcd = chb->dcd;
    old_cts = chb->cts;
    chb->dcd = (pins & Z80SIO_DCDB) != 0;
    chb->cts = (pins & Z80SIO_CTSB) != 0;

    if (chb->wr[1] & (1 << 0))
    {
        if (old_dcd != chb->dcd || old_cts != chb->cts)
        {
            chb->int_state |= Z80SIO_INT_NEEDED;
        }
    }

    if (chb->rts) {
        pins |= Z80SIO_RTSB;
    } else {
        pins &= ~Z80SIO_RTSB;
    }
    if (chb->dtr) {
        pins |= Z80SIO_DTRB;
    } else {
        pins &= ~Z80SIO_DTRB;
    }

    return pins;
}

/* Main tick function */
uint64_t z80sio_tick(z80sio_t *sio, uint64_t pins)
{
    // Handle reset
    if (pins & Z80SIO_RESET)
    {
        z80sio_reset(sio);
    }

    // Handle I/O operations
    if ((pins & (Z80SIO_CE | Z80SIO_IORQ | Z80SIO_M1)) == (Z80SIO_CE | Z80SIO_IORQ))
    {
        pins = _z80sio_io(sio, pins);
    }

    // Handle modem control signals
    pins = _z80sio_modem_control(sio, pins);

    // Handle interrupt daisy chain
    pins = _z80sio_int(sio, pins);

    sio->pins = pins;
    return pins;
}

/*
    Helper function to inject received data into a channel.
    Call this when data arrives from an external serial source.
    Will set RX ready flag and trigger interrupt if enabled.
*/
void z80sio_rx_data(z80sio_t *sio, int channel, uint8_t data)
{
    if (channel < 0 || channel >= Z80SIO_NUM_CHANNELS)
        return;

    z80sio_channel_t *ch = &sio->chn[channel];

    // Check if RX is enabled (WR3 bit 0)
    if (!(ch->wr[3] & (1 << 0)))
        return;

    // Check for overrun (data arrives while previous not read)
    if (ch->rx_ready)
    {
        ch->rx_overrun = true;
        _z80sio_maybe_raise_rx_interrupt(ch, true);
        return;
    }

    const uint8_t rx_bits = _z80sio_rx_bits_per_char(ch);
    const bool parity_enable = (ch->wr[4] & 0x01u) != 0;
    const bool parity_even = (ch->wr[4] & 0x02u) != 0;
    bool parity_error = false;

    if (parity_enable && (rx_bits < 8))
    {
        const uint8_t data_mask = (uint8_t)((1u << rx_bits) - 1u);
        const bool parity_bit = ((data >> rx_bits) & 0x01u) != 0;
        const bool data_odd = _z80sio_odd_parity_u8((uint8_t)(data & data_mask));
        const bool expected_parity_bit = parity_even ? data_odd : !data_odd;
        parity_error = parity_bit != expected_parity_bit;
    }

    if (parity_error)
        ch->parity_error = true;

    ch->rx_data = data;
    ch->rx_ready = true;

    _z80sio_maybe_raise_rx_interrupt(ch, _z80sio_special_rx_condition_no_parity(ch));
}

/*
    Helper function to check if transmit buffer is ready.
    Returns true if the channel can accept data for transmission.
*/
bool z80sio_tx_ready(z80sio_t *sio, int channel)
{
    if (channel < 0 || channel >= Z80SIO_NUM_CHANNELS)
        return false;

    z80sio_channel_t *ch = &sio->chn[channel];

    // Check if TX is enabled (WR5 bit 3)
    if (!(ch->wr[5] & (1 << 3)))
        return false;

    return ch->tx_ready;
}

/*
    Helper function to get transmitted data from a channel.
    Call this to retrieve data that was written by the CPU.
    Automatically sets tx_ready flag and triggers interrupt if enabled.
*/
uint8_t z80sio_tx_data(z80sio_t *sio, int channel)
{
    if (channel < 0 || channel >= Z80SIO_NUM_CHANNELS)
        return 0;

    z80sio_channel_t *ch = &sio->chn[channel];
    uint8_t data = ch->tx_data;

    // Mark TX buffer as empty
    ch->tx_ready = true;

    // Trigger TX buffer empty interrupt if enabled (WR1 bit 1)
    if (ch->wr[1] & (1 << 1))
    {
        ch->int_state |= Z80SIO_INT_NEEDED;
    }

    return data;
}

#endif // CHIPS_IMPL
