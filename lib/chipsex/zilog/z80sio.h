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
    - Three-byte receive FIFO with per-character error status
    - TX holding and shift registers with exact programmed frame duration
    - WR0–WR7 and RR0–RR2 programming behavior
    - Receive, transmit, and external/status interrupt priority
    - Modified vectors, daisy chaining, acknowledge, and RETI
    - Auto-enables, modem control, delayed RTS, and Wait/Ready
    - Simple pin-based tick-loop and complete-character line integration

    ## Not Implemented (yet):

    - Sync/SDLC/HDLC modes
    - Baud rate generation (requires external clock/CTC)
    - Bit-level modem/serial electrical timing (the complete-character line API
      models the same frame duration without exposing individual TxC/RxC edges)

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
#define Z80SIO_RX_FIFO_SIZE (3)

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
#define Z80SIO_PIN_WRDYA (51) // Wait/Ready A asserted (logical active state)
#define Z80SIO_PIN_WRDYB (52) // Wait/Ready B asserted (logical active state)

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
#define Z80SIO_WRDYA (1ULL << Z80SIO_PIN_WRDYA)
#define Z80SIO_WRDYB (1ULL << Z80SIO_PIN_WRDYB)

/* Interrupt sources, in descending priority within each channel. */
#define Z80SIO_INT_RECEIVE  (0)
#define Z80SIO_INT_TRANSMIT (1)
#define Z80SIO_INT_EXTERNAL (2)
#define Z80SIO_NUM_INT_SOURCES (3)

/* RR1 receive-error bits accepted by z80sio_rx_data_ex(). */
#define Z80SIO_RX_ERROR_PARITY  (1u << 4)
#define Z80SIO_RX_ERROR_OVERRUN (1u << 5)
#define Z80SIO_RX_ERROR_FRAMING (1u << 6)
#define Z80SIO_RX_END_OF_FRAME  (1u << 7)

#define Z80SIO_GET_DATA(p) ((uint8_t)(((p) >> 16) & 0xFF))
#define Z80SIO_SET_DATA(p, d)                                   \
    {                                                           \
        p = ((p) & ~0xFF0000ULL) | (((d) << 16) & 0xFF0000ULL); \
    }

    /* SIO Channel */
    typedef struct
    {
        uint8_t rx_data;
        uint8_t rx_fifo[Z80SIO_RX_FIFO_SIZE];
        uint8_t rx_parity_fifo[Z80SIO_RX_FIFO_SIZE];
        uint8_t rx_error_fifo[Z80SIO_RX_FIFO_SIZE];
        uint8_t rx_fifo_head;
        uint8_t rx_fifo_tail;
        uint8_t rx_fifo_count;
        uint8_t tx_data;
        bool rx_ready;          // RX character available
        bool tx_ready;          // TX buffer empty
        bool tx_shift_empty;    // Transmit shift register empty (RR1 D0)
        uint8_t wr[8];          // Write registers WR0-WR7
        uint8_t rr[3];          // Read registers RR0-RR2 (cached status)
        uint8_t reg_index;      // Currently selected register (from WR0)
        uint8_t int_vector;     // Interrupt vector
        uint8_t int_state;      // Interrupt state machine
        uint8_t int_source_state[Z80SIO_NUM_INT_SOURCES];
        uint8_t int_deferred;   // source bits held while M1 is active
        bool m1_active;

        // Status flags for RR0
        bool dcd;               // Data Carrier Detect
        bool cts;               // Clear To Send
        bool tx_underrun;       // TX underrun/EOM
        bool break_abort;       // Break/Abort detected
        bool send_break;        // WR5 D4 transmit spacing output
        bool sync_hunt;         // Sync/Hunt status (RR0 D4)
        bool ext_status_latched;// RR0 external/status fields are frozen
        uint8_t ext_status;     // Latched RR0 D7..D3 external/status fields

        // Status flags for RR1
        bool parity_error;      // Parity error
        bool rx_overrun;        // RX overrun error
        bool framing_error;     // Framing error
        bool end_of_frame;      // End of frame (SDLC)
        uint8_t residue_code;   // RR1 D1..D3

        // Control state
        bool rts;               // Request To Send (output)
        bool rts_requested;     // WR5 D1; deassertion waits for all-sent
        bool dtr;               // Data Terminal Ready (output)
        bool rx_int_on_first_armed; // WR1 mode 1 state
        bool rx_interrupt_special; // current RX request uses special vector
        bool tx_int_disarmed;   // WR0 CMD5 suppresses empty interrupts
        uint8_t reset_cooldown; // four system clocks after channel reset

        // Asynchronous shift-register timing. External devices only submit
        // and collect complete characters; the SIO owns their lifecycle.
        bool line_tx_active;
        uint8_t line_tx_data;
        uint64_t line_tx_complete_tick;
        bool line_tx_event_pending;
        uint8_t line_tx_event_data;
        bool line_rx_active;
        uint8_t line_rx_data;
        uint64_t line_rx_complete_tick;
        bool line_rx_event_pending;
        uint8_t line_rx_event_data;
        bool line_rx_event_accepted;
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

    // Clock only the interrupt daisy-chain pins (no bus or modem access).
    uint64_t z80sio_daisychain(z80sio_t *sio, uint64_t pins);

    // Helper functions to inject received data from external source
    void z80sio_rx_data(z80sio_t *sio, int channel, uint8_t data);
    void z80sio_rx_data_ex(z80sio_t *sio, int channel, uint8_t data,
                           uint8_t error_flags);

    // Helper function to check if TX is ready
    bool z80sio_tx_ready(z80sio_t *sio, int channel);

    // Helper function to get TX data
    uint8_t z80sio_tx_data(z80sio_t *sio, int channel);

    // Mark the current asynchronous character as fully shifted onto the wire.
    void z80sio_tx_complete(z80sio_t *sio, int channel);

    // Chip-owned channel state queries for external line adapters.
    bool z80sio_rx_enabled(const z80sio_t *sio, int channel);
    bool z80sio_rx_ready(const z80sio_t *sio, int channel);
    uint8_t z80sio_rx_fifo_count(const z80sio_t *sio, int channel);
    bool z80sio_tx_pending(const z80sio_t *sio, int channel);

    // Duration of one programmed asynchronous character in system ticks.
    uint64_t z80sio_character_ticks(const z80sio_t *sio, int channel,
                                    bool transmit, uint64_t system_hz,
                                    uint64_t serial_clock_hz);
    void z80sio_line_tick(z80sio_t *sio, int channel, uint64_t system_tick,
                          uint64_t system_hz, uint64_t serial_clock_hz);
    bool z80sio_line_receive(z80sio_t *sio, int channel, uint8_t data,
                             uint64_t system_tick, uint64_t system_hz,
                             uint64_t serial_clock_hz);
    bool z80sio_line_take_tx(z80sio_t *sio, int channel, uint8_t *data);
    bool z80sio_line_take_rx(z80sio_t *sio, int channel, uint8_t *data,
                             bool *accepted);
    bool z80sio_line_tx_busy(const z80sio_t *sio, int channel);
    bool z80sio_line_rx_busy(const z80sio_t *sio, int channel);

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
        memset(ch->rx_fifo, 0, sizeof(ch->rx_fifo));
        memset(ch->rx_parity_fifo, 0, sizeof(ch->rx_parity_fifo));
        memset(ch->rx_error_fifo, 0, sizeof(ch->rx_error_fifo));
        ch->rx_fifo_head = 0;
        ch->rx_fifo_tail = 0;
        ch->rx_fifo_count = 0;
        ch->tx_data = 0;
        ch->rx_ready = false;
        ch->tx_ready = true;
        ch->tx_shift_empty = true;

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
        memset(ch->int_source_state, 0, sizeof(ch->int_source_state));
        ch->int_deferred = 0;
        ch->m1_active = false;

        // Clear status flags
        ch->dcd = false;
        ch->cts = false;
        ch->tx_underrun = true; // RR0 D6 is set after reset
        ch->break_abort = false;
        ch->send_break = false;
        ch->sync_hunt = false;
        ch->ext_status_latched = false;
        ch->ext_status = 0;
        ch->parity_error = false;
        ch->rx_overrun = false;
        ch->framing_error = false;
        ch->end_of_frame = false;
        ch->residue_code = 0;

        // Reset control outputs
        ch->rts = false;
        ch->rts_requested = false;
        ch->dtr = false;
        ch->rx_int_on_first_armed = true;
        ch->rx_interrupt_special = false;
        ch->tx_int_disarmed = false;
        ch->reset_cooldown = 0;
        ch->line_tx_active = false;
        ch->line_tx_data = 0;
        ch->line_tx_complete_tick = 0;
        ch->line_tx_event_pending = false;
        ch->line_tx_event_data = 0;
        ch->line_rx_active = false;
        ch->line_rx_data = 0;
        ch->line_rx_complete_tick = 0;
        ch->line_rx_event_pending = false;
        ch->line_rx_event_data = 0;
        ch->line_rx_event_accepted = false;
    }
}

static inline int _z80sio_select_channel(uint64_t pins)
{
    return (pins & Z80SIO_CS_B) ? Z80SIO_CHANNEL_B : Z80SIO_CHANNEL_A;
}

static inline void _z80sio_sync_int_state(z80sio_channel_t *ch)
{
    ch->int_state = 0;
    for (int source = 0; source < Z80SIO_NUM_INT_SOURCES; ++source)
        ch->int_state |= ch->int_source_state[source];
}

static inline void _z80sio_request_source(z80sio_channel_t *ch, int source)
{
    if (ch->m1_active) {
        ch->int_deferred |= (uint8_t)(1u << source);
        return;
    }
    ch->int_source_state[source] |= Z80SIO_INT_NEEDED;
    _z80sio_sync_int_state(ch);
}

static inline void _z80sio_clear_source_pending(z80sio_channel_t *ch, int source)
{
    ch->int_source_state[source] &= (uint8_t)~(
        Z80SIO_INT_NEEDED | Z80SIO_INT_REQUESTED);
    ch->int_deferred &= (uint8_t)~(1u << source);
    _z80sio_sync_int_state(ch);
}

static inline void _z80sio_clear_source_service(z80sio_channel_t *ch,
                                                int source)
{
    ch->int_source_state[source] &= (uint8_t)~Z80SIO_INT_SERVICED;
    _z80sio_sync_int_state(ch);
}

static inline bool _z80sio_find_interrupt(const z80sio_t *sio,
                                          uint8_t state_mask,
                                          int *channel,
                                          int *source)
{
    static const uint8_t priority[6][2] = {
        { Z80SIO_CHANNEL_A, Z80SIO_INT_RECEIVE },
        { Z80SIO_CHANNEL_A, Z80SIO_INT_TRANSMIT },
        { Z80SIO_CHANNEL_A, Z80SIO_INT_EXTERNAL },
        { Z80SIO_CHANNEL_B, Z80SIO_INT_RECEIVE },
        { Z80SIO_CHANNEL_B, Z80SIO_INT_TRANSMIT },
        { Z80SIO_CHANNEL_B, Z80SIO_INT_EXTERNAL },
    };
    for (int i = 0; i < 6; ++i)
    {
        const int chn_id = priority[i][0];
        const int source_id = priority[i][1];
        if (sio->chn[chn_id].int_source_state[source_id] & state_mask)
        {
            *channel = chn_id;
            *source = source_id;
            return true;
        }
    }
    return false;
}

static inline bool _z80sio_auto_enables(const z80sio_channel_t *ch)
{
    return (ch->wr[3] & (1u << 5)) != 0;
}

static inline bool _z80sio_receive_allowed(const z80sio_channel_t *ch)
{
    return (ch->wr[3] & 0x01u) != 0u &&
        (!_z80sio_auto_enables(ch) || ch->dcd);
}

static inline bool _z80sio_transmit_allowed(const z80sio_channel_t *ch)
{
    return (ch->wr[5] & (1u << 3)) != 0u &&
        (!_z80sio_auto_enables(ch) || ch->cts);
}

static inline void _z80sio_load_front_rx_status(z80sio_channel_t *ch)
{
    uint8_t errors = 0;
    if (ch->rx_fifo_count != 0)
        errors = ch->rx_error_fifo[ch->rx_fifo_head];

    /* Framing and EOF describe only the character at the FIFO head. Parity
       and overrun have separate cumulative latches set on reception. */
    ch->framing_error = (errors & Z80SIO_RX_ERROR_FRAMING) != 0;
    ch->end_of_frame = (errors & Z80SIO_RX_END_OF_FRAME) != 0;
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

static inline void _z80sio_maybe_raise_rx_interrupt(z80sio_channel_t *ch,
                                                    bool special)
{
    bool request = false;
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
            request = true;
            ch->rx_int_on_first_armed = false;
        }
        else if (special)
            request = true;
        break;
    case 2:
    case 3:
        request = true;
        break;
    default:
        break;
    }
    if (request)
    {
        ch->rx_interrupt_special = ch->rx_interrupt_special || special;
        _z80sio_request_source(ch, Z80SIO_INT_RECEIVE);
    }
}

/* Update RR0 status register */
static inline void _z80sio_update_rr0(z80sio_t *sio,
                                     z80sio_channel_t *ch,
                                     int chn_id)
{
    ch->rr[0] = 0;
    if (ch->rx_ready) ch->rr[0] |= (1 << 0);        // RX character available
    if (chn_id == Z80SIO_CHANNEL_A)
    {
        const uint8_t pending = (uint8_t)(
            sio->chn[Z80SIO_CHANNEL_A].int_state |
            sio->chn[Z80SIO_CHANNEL_B].int_state
        );
        if (pending & (Z80SIO_INT_NEEDED | Z80SIO_INT_REQUESTED))
            ch->rr[0] |= (1 << 1);                   // Any SIO interrupt pending
    }
    if (ch->tx_ready) ch->rr[0] |= (1 << 2);        // TX buffer empty
    uint8_t external = 0;
    if (ch->dcd) external |= (1 << 3);
    if (ch->sync_hunt) external |= (1 << 4);
    if (ch->cts) external |= (1 << 5);
    if (ch->tx_underrun) external |= (1 << 6);
    if (ch->break_abort) external |= (1 << 7);
    ch->rr[0] |= ch->ext_status_latched ? ch->ext_status : external;
}

/* Update RR1 status register */
static inline void _z80sio_update_rr1(z80sio_channel_t *ch)
{
    const uint8_t current_errors = ch->rx_fifo_count != 0
        ? ch->rx_error_fifo[ch->rx_fifo_head] : 0;
    ch->rr[1] = 0;
    if (ch->tx_ready && ch->tx_shift_empty)
        ch->rr[1] |= (1 << 0);                       // All sent
    ch->rr[1] |= (uint8_t)((ch->residue_code & 0x07u) << 1); // SDLC residue
    if (ch->parity_error || (current_errors & Z80SIO_RX_ERROR_PARITY))
        ch->rr[1] |= (1 << 4);                       // Parity error
    if (ch->rx_overrun || (current_errors & Z80SIO_RX_ERROR_OVERRUN))
        ch->rr[1] |= (1 << 5);                       // RX overrun error
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
            /* SDLC serialization is outside the complete-character API. */
            break;
        case 2: // Reset External/Status interrupts
            ch->ext_status_latched = false;
            _z80sio_clear_source_pending(ch, Z80SIO_INT_EXTERNAL);
            break;
        case 3: // Channel reset
            {
                const uint8_t base_vector =
                    sio->chn[Z80SIO_CHANNEL_B].int_vector;
                memset(ch, 0, sizeof(*ch));
                ch->tx_ready = true;
                ch->tx_shift_empty = true;
                ch->tx_underrun = true;
                ch->rx_int_on_first_armed = true;
                /* WR2 is the shared interrupt-vector register, accessed
                   through Channel B; a single-channel reset must not erase
                   the other channel's vectoring state. */
                ch->int_vector = base_vector;
                ch->reset_cooldown = 4;
                if (chn_id == Z80SIO_CHANNEL_A)
                {
                    /* Channel A reset also resets the internal interrupt
                       prioritization logic for both channels. */
                    memset(sio->chn[Z80SIO_CHANNEL_B].int_source_state, 0,
                           sizeof(sio->chn[Z80SIO_CHANNEL_B].int_source_state));
                    _z80sio_sync_int_state(&sio->chn[Z80SIO_CHANNEL_B]);
                }
                else
                {
                    sio->chn[Z80SIO_CHANNEL_A].int_vector = base_vector;
                }
            }
            break;
        case 4: // Enable interrupt on next receive character
            ch->rx_int_on_first_armed = true;
            break;
        case 5: // Reset TX interrupt pending
            _z80sio_clear_source_pending(ch, Z80SIO_INT_TRANSMIT);
            ch->tx_int_disarmed = true;
            break;
        case 6: // Error reset (latches)
            ch->parity_error = false;
            ch->rx_overrun = false;
            ch->residue_code = 0;
            /* Buffered per-character flags remain associated with their
               characters; reset only the cumulative RR1 latches. */
            _z80sio_load_front_rx_status(ch);
            break;
        case 7: // Return from interrupt (Channel A command path)
            if (chn_id == Z80SIO_CHANNEL_A)
            {
                int service_channel = 0;
                int service_source = 0;
                if (_z80sio_find_interrupt(sio, Z80SIO_INT_SERVICED,
                                           &service_channel, &service_source))
                    _z80sio_clear_source_service(
                        &sio->chn[service_channel], service_source);
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
            ch->rts_requested = (data & (1 << 1)) != 0;
            if (ch->rts_requested)
                ch->rts = true;
            else if (ch->tx_ready && ch->tx_shift_empty)
                ch->rts = false;
            // Bit 2: CRC-16/SDLC polynomial select
            // Bit 3: TX enable
            // Bit 4: Send break
            ch->send_break = (data & (1u << 4)) != 0;
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

static inline uint8_t _z80sio_get_int_vector(z80sio_t *sio, int chn_id,
                                             int source);

/* Read from control/data register */
static inline uint8_t _z80sio_read(z80sio_t *sio, int chn_id, bool control)
{
    z80sio_channel_t *ch = &sio->chn[chn_id];

    if (control)
    {
        // Read from control (status) register
        // Reading RR0-RR2 based on reg_index
        _z80sio_update_rr0(sio, ch, chn_id);
        _z80sio_update_rr1(ch);

        uint8_t reg = ch->reg_index;
        if (reg > 2) reg = 0; // Only RR0-RR2 exist

        uint8_t data = ch->rr[reg];
        if (reg == 2 && chn_id == Z80SIO_CHANNEL_B)
        {
            int pending_channel = Z80SIO_CHANNEL_B;
            int pending_source = Z80SIO_INT_EXTERNAL;
            if (_z80sio_find_interrupt(
                    sio, Z80SIO_INT_NEEDED | Z80SIO_INT_REQUESTED |
                         Z80SIO_INT_SERVICED,
                    &pending_channel, &pending_source))
                data = _z80sio_get_int_vector(
                    sio, pending_channel, pending_source);
            else if (sio->chn[Z80SIO_CHANNEL_B].wr[1] & (1u << 2))
                /* With no pending cause, RR2 reports the documented Channel
                   B special-receive encoding (V3..V1 = 011). */
                data = (uint8_t)(
                    sio->chn[Z80SIO_CHANNEL_B].int_vector & 0xF1u) | 0x06u;
            else
                data = sio->chn[Z80SIO_CHANNEL_B].int_vector;
        }
        ch->reg_index = 0; // Auto-reset to RR0
        return data;
    }
    else
    {
        // Read from data register
        uint8_t data = ch->rx_data;
        if (ch->rx_fifo_count > 0)
        {
            ch->rx_fifo_head = (uint8_t)((ch->rx_fifo_head + 1u) % Z80SIO_RX_FIFO_SIZE);
            ch->rx_fifo_count--;
        }
        ch->rx_ready = ch->rx_fifo_count != 0;
        if (ch->rx_ready)
        {
            ch->rx_data = ch->rx_fifo[ch->rx_fifo_head];
        }
        else
        {
            ch->rx_data = 0;
        }
        _z80sio_load_front_rx_status(ch);
        _z80sio_clear_source_pending(ch, Z80SIO_INT_RECEIVE);
        ch->rx_interrupt_special = false;
        /* In interrupt-on-all modes another queued character immediately
           presents another receive cause after the current one is read. */
        if (ch->rx_ready && _z80sio_rx_int_mode(ch) >= 2)
        {
            const uint8_t errors = ch->rx_error_fifo[ch->rx_fifo_head];
            const bool parity_special = _z80sio_rx_int_mode(ch) == 2 &&
                (errors & Z80SIO_RX_ERROR_PARITY) != 0;
            _z80sio_maybe_raise_rx_interrupt(ch,
                parity_special || (errors & (Z80SIO_RX_ERROR_OVERRUN |
                    Z80SIO_RX_ERROR_FRAMING | Z80SIO_RX_END_OF_FRAME)) != 0);
        }
        return data;
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
        ch->tx_int_disarmed = false;
        _z80sio_clear_source_pending(ch, Z80SIO_INT_TRANSMIT);
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
        uint8_t data = _z80sio_read(sio, chn_id, control);
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
static inline uint8_t _z80sio_get_int_vector(z80sio_t *sio, int chn_id,
                                             int source)
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

    // Type bits (bits 1..2): 00=TX, 01=Ext/Status, 10=RX, 11=Special RX.
    if (source == Z80SIO_INT_RECEIVE)
        modified |= (ch->rx_interrupt_special ? 3u : 2u) << 1;
    else if (source == Z80SIO_INT_EXTERNAL)
        modified |= 1u << 1;

    return modified;
}

/* Interrupt daisy chain handling */
static inline uint64_t _z80sio_int(z80sio_t *sio, uint64_t pins)
{
    int chn_id = 0;
    int source = 0;

    if (pins & Z80SIO_RETI)
    {
        if (_z80sio_find_interrupt(sio, Z80SIO_INT_SERVICED,
                                   &chn_id, &source))
        {
            _z80sio_clear_source_service(&sio->chn[chn_id], source);
            pins &= ~Z80SIO_RETI;
        }
    }

    if (!_z80sio_find_interrupt(
            sio, Z80SIO_INT_NEEDED | Z80SIO_INT_REQUESTED |
                 Z80SIO_INT_SERVICED,
            &chn_id, &source) || !(pins & Z80SIO_IEIO))
        return pins;

    z80sio_channel_t *ch = &sio->chn[chn_id];
    uint8_t *state = &ch->int_source_state[source];
    pins &= ~Z80SIO_IEIO;

    if (*state & (Z80SIO_INT_NEEDED | Z80SIO_INT_REQUESTED))
        pins |= Z80SIO_INT;

    if (*state & Z80SIO_INT_NEEDED)
        *state = (uint8_t)((*state & ~Z80SIO_INT_NEEDED) |
                           Z80SIO_INT_REQUESTED);

    if ((*state & Z80SIO_INT_REQUESTED) &&
        ((pins & (Z80SIO_IORQ | Z80SIO_M1)) ==
         (Z80SIO_IORQ | Z80SIO_M1)))
    {
        Z80SIO_SET_DATA(pins, _z80sio_get_int_vector(
            sio, chn_id, source));
        *state = (uint8_t)((*state & ~Z80SIO_INT_REQUESTED) |
                           Z80SIO_INT_SERVICED);
        pins &= ~Z80SIO_INT;
    }
    _z80sio_sync_int_state(ch);
    return pins;
}

uint64_t z80sio_daisychain(z80sio_t *sio, uint64_t pins)
{
    pins = _z80sio_int(sio, pins);
    sio->pins = pins;
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

    // A first transition freezes all External/Status fields until CMD2.
    if ((old_dcd != cha->dcd || old_cts != cha->cts) &&
        !cha->ext_status_latched)
    {
        cha->ext_status = (uint8_t)(
            (cha->dcd ? (1u << 3) : 0u) |
            (cha->sync_hunt ? (1u << 4) : 0u) |
            (cha->cts ? (1u << 5) : 0u) |
            (cha->tx_underrun ? (1u << 6) : 0u) |
            (cha->break_abort ? (1u << 7) : 0u));
        cha->ext_status_latched = true;
        if (cha->wr[1] & (1u << 0))
            _z80sio_request_source(cha, Z80SIO_INT_EXTERNAL);
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

    if ((old_dcd != chb->dcd || old_cts != chb->cts) &&
        !chb->ext_status_latched)
    {
        chb->ext_status = (uint8_t)(
            (chb->dcd ? (1u << 3) : 0u) |
            (chb->sync_hunt ? (1u << 4) : 0u) |
            (chb->cts ? (1u << 5) : 0u) |
            (chb->tx_underrun ? (1u << 6) : 0u) |
            (chb->break_abort ? (1u << 7) : 0u));
        chb->ext_status_latched = true;
        if (chb->wr[1] & (1u << 0))
            _z80sio_request_source(chb, Z80SIO_INT_EXTERNAL);
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

static inline uint64_t _z80sio_wait_ready(z80sio_t *sio, uint64_t pins)
{
    const uint64_t masks[Z80SIO_NUM_CHANNELS] = {
        Z80SIO_WRDYA, Z80SIO_WRDYB
    };
    pins &= ~(Z80SIO_WRDYA | Z80SIO_WRDYB);
    for (int chn_id = 0; chn_id < Z80SIO_NUM_CHANNELS; ++chn_id)
    {
        const z80sio_channel_t *ch = &sio->chn[chn_id];
        if (!(ch->wr[1] & (1u << 7)))
            continue;

        const bool receive = (ch->wr[1] & (1u << 5)) != 0;
        const bool ready_function = (ch->wr[1] & (1u << 6)) != 0;
        const bool transfer_ready = receive ? ch->rx_ready : ch->tx_ready;
        bool asserted = false;
        if (ready_function)
        {
            asserted = transfer_ready;
        }
        else
        {
            const bool selected =
                (pins & (Z80SIO_CE | Z80SIO_IORQ)) ==
                    (Z80SIO_CE | Z80SIO_IORQ) &&
                _z80sio_select_channel(pins) == chn_id &&
                !(pins & Z80SIO_CS_A);
            asserted = selected && !transfer_ready;
        }
        if (asserted)
            pins |= masks[chn_id];
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

    /* Z80 daisy devices inhibit new interrupt requests throughout M1. Events
       are retained and become visible on the first clock after M1 releases. */
    const bool m1_now = (pins & Z80SIO_M1) != 0;
    for (int i = 0; i < Z80SIO_NUM_CHANNELS; ++i) {
        z80sio_channel_t *ch = &sio->chn[i];
        if (!m1_now && ch->m1_active && ch->int_deferred) {
            for (int source = 0; source < Z80SIO_NUM_INT_SOURCES; ++source) {
                if (ch->int_deferred & (uint8_t)(1u << source))
                    ch->int_source_state[source] |= Z80SIO_INT_NEEDED;
            }
            ch->int_deferred = 0;
            _z80sio_sync_int_state(ch);
        }
        ch->m1_active = m1_now;
    }

    // Modem state is sampled before an I/O access so RR0 and Auto Enables see
    // the levels present on this clock.
    pins = _z80sio_modem_control(sio, pins);

    bool cooldown_blocked[Z80SIO_NUM_CHANNELS] = { false, false };
    for (int i = 0; i < Z80SIO_NUM_CHANNELS; ++i)
    {
        cooldown_blocked[i] = sio->chn[i].reset_cooldown != 0;
        if (cooldown_blocked[i])
            --sio->chn[i].reset_cooldown;
    }

    /* WAIT is decided from the buffer state before the transfer. A blocked
       access is retried by the CPU and must not consume or overwrite data. */
    pins = _z80sio_wait_ready(sio, pins);
    const int selected_channel = _z80sio_select_channel(pins);
    const uint64_t selected_wrdy = selected_channel == Z80SIO_CHANNEL_A
        ? Z80SIO_WRDYA : Z80SIO_WRDYB;
    const bool wait_function =
        (sio->chn[selected_channel].wr[1] & 0xC0u) == 0x80u;
    const bool wait_requested = wait_function && (pins & selected_wrdy);

    // Handle I/O operations.
    if ((pins & (Z80SIO_CE | Z80SIO_IORQ | Z80SIO_M1)) ==
            (Z80SIO_CE | Z80SIO_IORQ) &&
        !cooldown_blocked[selected_channel] && !wait_requested)
    {
        pins = _z80sio_io(sio, pins);
    }

    /* READY is asynchronous to chip selection and follows the state after a
       completed transfer. WAIT remains the pre-transfer decision above. */
    const uint64_t ready_masks[Z80SIO_NUM_CHANNELS] = {
        Z80SIO_WRDYA, Z80SIO_WRDYB
    };
    for (int i = 0; i < Z80SIO_NUM_CHANNELS; ++i)
    {
        const z80sio_channel_t *ch = &sio->chn[i];
        if ((ch->wr[1] & 0xC0u) != 0xC0u)
            continue;
        const bool receive = (ch->wr[1] & (1u << 5)) != 0;
        const bool transfer_ready = receive ? ch->rx_ready : ch->tx_ready;
        if (transfer_ready)
            pins |= ready_masks[i];
        else
            pins &= ~ready_masks[i];
    }

    // Handle interrupt daisy chain
    pins = z80sio_daisychain(sio, pins);

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

    if (!_z80sio_receive_allowed(ch))
        return;

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

    z80sio_rx_data_ex(sio, channel, data,
        parity_error ? Z80SIO_RX_ERROR_PARITY : 0u);
}

void z80sio_rx_data_ex(z80sio_t *sio, int channel, uint8_t data,
                       uint8_t error_flags)
{
    if (!sio || channel < 0 || channel >= Z80SIO_NUM_CHANNELS)
        return;

    z80sio_channel_t *ch = &sio->chn[channel];
    if (!_z80sio_receive_allowed(ch))
        return;

    error_flags &= (uint8_t)(Z80SIO_RX_ERROR_PARITY |
        Z80SIO_RX_ERROR_OVERRUN | Z80SIO_RX_ERROR_FRAMING |
        Z80SIO_RX_END_OF_FRAME);

    // The fourth completed character replaces the third FIFO character.
    if (ch->rx_fifo_count >= Z80SIO_RX_FIFO_SIZE)
    {
        const uint8_t newest = (uint8_t)(
            (ch->rx_fifo_tail + Z80SIO_RX_FIFO_SIZE - 1u) %
            Z80SIO_RX_FIFO_SIZE
        );
        ch->rx_fifo[newest] = data;
        error_flags |= Z80SIO_RX_ERROR_OVERRUN;
        ch->rx_error_fifo[newest] = error_flags;
        ch->rx_parity_fifo[newest] =
            (error_flags & Z80SIO_RX_ERROR_PARITY) != 0;
        if (error_flags & Z80SIO_RX_ERROR_PARITY)
            ch->parity_error = true;
        if (error_flags & Z80SIO_RX_ERROR_OVERRUN)
            ch->rx_overrun = true;
        const bool special =
            (error_flags & (Z80SIO_RX_ERROR_OVERRUN |
                Z80SIO_RX_ERROR_FRAMING | Z80SIO_RX_END_OF_FRAME)) != 0 ||
            (_z80sio_rx_int_mode(ch) == 2 &&
                (error_flags & Z80SIO_RX_ERROR_PARITY) != 0);
        _z80sio_maybe_raise_rx_interrupt(ch, special);
        return;
    }

    ch->rx_fifo[ch->rx_fifo_tail] = data;
    ch->rx_error_fifo[ch->rx_fifo_tail] = error_flags;
    ch->rx_parity_fifo[ch->rx_fifo_tail] =
        (error_flags & Z80SIO_RX_ERROR_PARITY) != 0;
    ch->rx_fifo_tail = (uint8_t)((ch->rx_fifo_tail + 1u) % Z80SIO_RX_FIFO_SIZE);
    ch->rx_fifo_count++;
    if (error_flags & Z80SIO_RX_ERROR_PARITY)
        ch->parity_error = true;
    if (error_flags & Z80SIO_RX_ERROR_OVERRUN)
        ch->rx_overrun = true;
    if (ch->rx_fifo_count == 1u)
    {
        ch->rx_data = data;
    }
    ch->rx_ready = true;
    _z80sio_load_front_rx_status(ch);

    const bool special =
        (error_flags & (Z80SIO_RX_ERROR_OVERRUN |
            Z80SIO_RX_ERROR_FRAMING | Z80SIO_RX_END_OF_FRAME)) != 0 ||
        (_z80sio_rx_int_mode(ch) == 2 &&
            (error_flags & Z80SIO_RX_ERROR_PARITY) != 0);
    _z80sio_maybe_raise_rx_interrupt(ch, special);
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

    // Moving the holding-register byte into the shift register makes the
    // holding register empty immediately, but RR1 "all sent" remains clear
    // until the caller reports that the complete character reached the wire.
    ch->tx_ready = true;
    ch->tx_shift_empty = false;

    // Trigger TX buffer empty interrupt if enabled (WR1 bit 1)
    if ((ch->wr[1] & (1 << 1)) && !ch->tx_int_disarmed)
        _z80sio_request_source(ch, Z80SIO_INT_TRANSMIT);

    return data;
}

void z80sio_tx_complete(z80sio_t *sio, int channel)
{
    if (channel < 0 || channel >= Z80SIO_NUM_CHANNELS)
        return;

    z80sio_channel_t *ch = &sio->chn[channel];
    ch->tx_shift_empty = true;

    /* In asynchronous mode the SIO delays RTS deassertion until the current
       character and any buffered successor are completely transmitted. */
    if (!ch->rts_requested && ch->tx_ready)
        ch->rts = false;
}

bool z80sio_rx_enabled(const z80sio_t *sio, int channel)
{
    if (!sio || channel < 0 || channel >= Z80SIO_NUM_CHANNELS)
        return false;
    return _z80sio_receive_allowed(&sio->chn[channel]);
}

bool z80sio_rx_ready(const z80sio_t *sio, int channel)
{
    if (!sio || channel < 0 || channel >= Z80SIO_NUM_CHANNELS)
        return false;
    return sio->chn[channel].rx_ready;
}

uint8_t z80sio_rx_fifo_count(const z80sio_t *sio, int channel)
{
    if (!sio || channel < 0 || channel >= Z80SIO_NUM_CHANNELS)
        return 0;
    return sio->chn[channel].rx_fifo_count;
}

bool z80sio_tx_pending(const z80sio_t *sio, int channel)
{
    if (!sio || channel < 0 || channel >= Z80SIO_NUM_CHANNELS)
        return false;
    const z80sio_channel_t *ch = &sio->chn[channel];
    return !ch->tx_ready && _z80sio_transmit_allowed(ch);
}

static inline uint64_t _z80sio_gcd_u64(uint64_t a, uint64_t b)
{
    while (b != 0)
    {
        const uint64_t remainder = a % b;
        a = b;
        b = remainder;
    }
    return a;
}

uint64_t z80sio_character_ticks(const z80sio_t *sio, int channel,
                                bool transmit, uint64_t system_hz,
                                uint64_t serial_clock_hz)
{
    static const uint8_t clock_divisors[4] = { 1, 16, 32, 64 };
    static const uint8_t bits_per_character[4] = { 5, 7, 6, 8 };
    static const uint8_t stop_half_bits[4] = { 0, 2, 3, 4 };

    if (!sio || channel < 0 || channel >= Z80SIO_NUM_CHANNELS ||
        system_hz == 0 || serial_clock_hz == 0)
        return 1;

    const z80sio_channel_t *ch = &sio->chn[channel];
    const uint8_t clock_code = (uint8_t)((ch->wr[4] >> 6) & 0x03u);
    const uint8_t size_code = transmit
        ? (uint8_t)((ch->wr[5] >> 5) & 0x03u)
        : (uint8_t)((ch->wr[3] >> 6) & 0x03u);
    const uint8_t stop_code = (uint8_t)((ch->wr[4] >> 2) & 0x03u);
    uint64_t half_bits = (uint64_t)bits_per_character[size_code] * 2u;

    if ((ch->wr[4] & 0x01u) != 0u)
        half_bits += 2u;
    if (stop_code != 0)
        half_bits += 2u; /* asynchronous start bit */
    half_bits += stop_half_bits[stop_code];

    if (serial_clock_hz > UINT64_MAX / 2u)
        return 1;
    uint64_t denominator = serial_clock_hz * 2u;
    uint64_t factors[3] = {
        system_hz, half_bits, clock_divisors[clock_code]
    };
    for (int i = 0; i < 3; ++i)
    {
        const uint64_t divisor = _z80sio_gcd_u64(factors[i], denominator);
        factors[i] /= divisor;
        denominator /= divisor;
    }
    uint64_t numerator = 1;
    for (int i = 0; i < 3; ++i)
    {
        if (factors[i] != 0 && numerator > UINT64_MAX / factors[i])
            return UINT64_MAX;
        numerator *= factors[i];
    }
    const uint64_t ticks = numerator / denominator +
        (numerator % denominator != 0 ? 1u : 0u);
    return ticks ? ticks : 1;
}

void z80sio_line_tick(z80sio_t *sio, int channel, uint64_t system_tick,
                      uint64_t system_hz, uint64_t serial_clock_hz)
{
    if (!sio || channel < 0 || channel >= Z80SIO_NUM_CHANNELS)
        return;
    z80sio_channel_t *ch = &sio->chn[channel];

    if (ch->line_tx_active && system_tick >= ch->line_tx_complete_tick) {
        ch->line_tx_active = false;
        ch->line_tx_event_data = ch->line_tx_data;
        ch->line_tx_event_pending = true;
        z80sio_tx_complete(sio, channel);
    }
    if (!ch->line_tx_active && z80sio_tx_pending(sio, channel)) {
        ch->line_tx_data = z80sio_tx_data(sio, channel);
        ch->line_tx_active = true;
        ch->line_tx_complete_tick = system_tick + z80sio_character_ticks(
            sio, channel, true, system_hz, serial_clock_hz);
    }

    if (ch->line_rx_active && system_tick >= ch->line_rx_complete_tick) {
        const uint8_t data = ch->line_rx_data;
        ch->line_rx_active = false;
        const bool accepted = _z80sio_receive_allowed(ch);
        if (accepted)
            z80sio_rx_data(sio, channel, data);
        ch->line_rx_event_data = data;
        ch->line_rx_event_accepted = accepted;
        ch->line_rx_event_pending = true;
    }
}

bool z80sio_line_receive(z80sio_t *sio, int channel, uint8_t data,
                         uint64_t system_tick, uint64_t system_hz,
                         uint64_t serial_clock_hz)
{
    if (!sio || channel < 0 || channel >= Z80SIO_NUM_CHANNELS)
        return false;
    z80sio_channel_t *ch = &sio->chn[channel];
    if (ch->line_rx_active || !_z80sio_receive_allowed(ch))
        return false;
    ch->line_rx_active = true;
    ch->line_rx_data = data;
    ch->line_rx_complete_tick = system_tick + z80sio_character_ticks(
        sio, channel, false, system_hz, serial_clock_hz);
    return true;
}

bool z80sio_line_take_tx(z80sio_t *sio, int channel, uint8_t *data)
{
    if (!sio || !data || channel < 0 || channel >= Z80SIO_NUM_CHANNELS)
        return false;
    z80sio_channel_t *ch = &sio->chn[channel];
    if (!ch->line_tx_event_pending)
        return false;
    *data = ch->line_tx_event_data;
    ch->line_tx_event_pending = false;
    return true;
}

bool z80sio_line_take_rx(z80sio_t *sio, int channel, uint8_t *data,
                         bool *accepted)
{
    if (!sio || !data || !accepted || channel < 0 ||
        channel >= Z80SIO_NUM_CHANNELS)
        return false;
    z80sio_channel_t *ch = &sio->chn[channel];
    if (!ch->line_rx_event_pending)
        return false;
    *data = ch->line_rx_event_data;
    *accepted = ch->line_rx_event_accepted;
    ch->line_rx_event_pending = false;
    return true;
}

bool z80sio_line_tx_busy(const z80sio_t *sio, int channel)
{
    if (!sio || channel < 0 || channel >= Z80SIO_NUM_CHANNELS)
        return false;
    return sio->chn[channel].line_tx_active ||
           z80sio_tx_pending(sio, channel);
}

bool z80sio_line_rx_busy(const z80sio_t *sio, int channel)
{
    if (!sio || channel < 0 || channel >= Z80SIO_NUM_CHANNELS)
        return false;
    return sio->chn[channel].line_rx_active;
}

#endif // CHIPS_IMPL
