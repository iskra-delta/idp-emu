/*#
    # z80dma.h

    Header-only pin-perfect emulator for the Zilog Z80 DMA (Direct Memory Access)
    controller written in C.

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

    ****************************************************
    *           +-----------+                          *
    *   A0..A15 |           | <--> Memory / I/O        *
    *   D0..D7  |           | <--> Memory / I/O        *
    *   MREQ <--|           |<--- RD                   *
    *   IORQ <--|    Z80    |<--- WR                   *
    *  BUSREQ <-|    DMA    |---> BUSACK               *
    *    INT <- |           |<--- IEI (daisy chain)    *
    *    M1 --> |           |---> IEO (daisy chain)    *
    *  RESET -->|           |<--- CE (chip enable)     *
    *           +-----------+                          *
    ****************************************************

    ## Supported Features:

    - Register-based programming (WR0-WR6)
    - Read status and byte counter
    - Port A and Port B architecture
    - Memory-to-memory, memory-to-I/O, I/O-to-memory transfers
    - Byte, continuous, and burst transfer modes
    - Auto-restart capability
    - Interrupt on end of block
    - Full BUSREQ/BUSACK handshake
    - Interrupt daisy chain protocol

    ## Not Implemented (yet):

    - Search and search/transfer execution (the programming registers decode,
      but the transfer engine currently implements sequential transfers)
    - CE/WAIT cycle extension
    - Half-clock early bus-control termination

    ## Usage:

    Initialize and program via registers:

    ~~~C
    z80dma_t dma;
    uint64_t pins = z80dma_init(&dma);

    // Program DMA via WR0-WR6 registers (see Z80 DMA datasheet)
    // Example: Transfer 256 bytes from memory 0x8000 to I/O port 0x10
    pins = z80dma_write(&dma, 0x79);  // WR0: Transfer mode, port A->B
    pins = z80dma_write(&dma, 0x00);  // Port A start address low
    pins = z80dma_write(&dma, 0x80);  // Port A start address high
    pins = z80dma_write(&dma, 0x00);  // Block length low
    pins = z80dma_write(&dma, 0x01);  // Block length high (256)

    pins = z80dma_write(&dma, 0x14);  // WR1: Port A is memory, increment
    pins = z80dma_write(&dma, 0x28);  // WR2: Port B is I/O, fixed address
    pins = z80dma_write(&dma, 0x85);  // WR4: Byte mode, B low address follows
    pins = z80dma_write(&dma, 0x10);  // Port B address
    pins = z80dma_write(&dma, 0x8A);  // WR5: RDY active high, CE only
    pins = z80dma_write(&dma, 0xCF);  // WR6: Load addresses and byte counter
    pins = z80dma_write(&dma, 0x05);  // WR0: Port A is the source
    pins = z80dma_write(&dma, 0xCF);  // WR6: Reload after direction change
    pins = z80dma_write(&dma, 0x87);  // WR6: Enable DMA
    ~~~

    On each system tick:

    ~~~C
    pins = z80dma_tick(&dma, pins);
    ~~~

    ## License:

    zlib/libpng license

    Copyright (c) 2025 Tomaz Stih

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

/* Pin definitions (compatible with Z80 CPU pins) */
#define Z80DMA_PIN_M1       (24)
#define Z80DMA_PIN_IORQ     (26)
#define Z80DMA_PIN_RD       (27)
#define Z80DMA_PIN_WR       (28)
#define Z80DMA_PIN_INT      (30)
#define Z80DMA_PIN_RESET    (31)

/* Interrupt daisy chain pins */
#define Z80DMA_PIN_IEIO     (37)  // Combined IEI/IEO
#define Z80DMA_PIN_RETI     (38)  // Return from interrupt

/* DMA-specific pins */
#define Z80DMA_PIN_CE       (40)  // Chip enable
#define Z80DMA_PIN_BUSREQ   (41)  // Bus request output
#define Z80DMA_PIN_BUSACK   (42)  // Bus acknowledge input
#define Z80DMA_PIN_BAO      (43)  // Bus acknowledge output (for daisy)
#define Z80DMA_PIN_RDY      (44)  // External source/destination ready

/* Pin masks */
#define Z80DMA_M1           (1ULL << Z80DMA_PIN_M1)
#define Z80DMA_IORQ         (1ULL << Z80DMA_PIN_IORQ)
#define Z80DMA_RD           (1ULL << Z80DMA_PIN_RD)
#define Z80DMA_WR           (1ULL << Z80DMA_PIN_WR)
#define Z80DMA_INT          (1ULL << Z80DMA_PIN_INT)
#define Z80DMA_RESET        (1ULL << Z80DMA_PIN_RESET)
#define Z80DMA_IEIO         (1ULL << Z80DMA_PIN_IEIO)
#define Z80DMA_RETI         (1ULL << Z80DMA_PIN_RETI)
#define Z80DMA_CE           (1ULL << Z80DMA_PIN_CE)
#define Z80DMA_BUSREQ       (1ULL << Z80DMA_PIN_BUSREQ)
#define Z80DMA_BUSACK       (1ULL << Z80DMA_PIN_BUSACK)
#define Z80DMA_BAO          (1ULL << Z80DMA_PIN_BAO)
#define Z80DMA_RDY          (1ULL << Z80DMA_PIN_RDY)

/* Also support old MREQ */
#define Z80DMA_MREQ         (1ULL << 34)

/* Pin helper macros */
#define Z80DMA_GET_DATA(p) ((uint8_t)(((p) >> 16) & 0xFF))
#define Z80DMA_SET_DATA(p, d) { p = ((p) & ~0xFF0000ULL) | (((uint64_t)(d) << 16) & 0xFF0000ULL); }
#define Z80DMA_SET_ADDR(p, a) { p = ((p) & ~0xFFFFULL) | ((a) & 0xFFFFULL); }
#define Z80DMA_GET_ADDR(p) ((uint16_t)((p) & 0xFFFFULL))

/* Port configuration */
typedef struct {
    uint16_t address;           // Current address
    uint16_t start_address;     // Start address (for reload)
    uint16_t block_length;      // Block length counter
    uint16_t start_length;      // Start length (for reload)
    uint8_t timing;             // Timing register
    bool is_memory;             // true=memory, false=I/O
    bool increment;             // Address increment
    bool decrement;             // Address decrement
} z80dma_port_t;

/* DMA state machine */
typedef enum {
    Z80DMA_STATE_IDLE,          // Waiting for enable
    Z80DMA_STATE_WAIT_BUS,      // Waiting for BUSACK
    Z80DMA_STATE_READ,          // Driving source address/read strobe
    Z80DMA_STATE_READ_LATCH,    // Sampling source data after the bus responds
    Z80DMA_STATE_WRITE,         // Writing to destination
    Z80DMA_STATE_WAIT_READY,    // Continuous mode owns bus while RDY is inactive
    Z80DMA_STATE_SEARCH,        // Searching/comparing data
    Z80DMA_STATE_VERIFY,        // Verify mode
} z80dma_state_t;

/* Transfer mode */
typedef enum {
    Z80DMA_MODE_BYTE,           // Single byte transfer
    Z80DMA_MODE_CONTINUOUS,     // Continuous until block done
    Z80DMA_MODE_BURST,          // Burst mode
} z80dma_transfer_mode_t;

/* Interrupt state */
#define Z80DMA_INT_NEEDED       (1 << 0)
#define Z80DMA_INT_REQUESTED    (1 << 1)
#define Z80DMA_INT_SERVICED     (1 << 2)

/* Status flags */
#define Z80DMA_STATUS_MATCH     (1 << 3)   // Match found during search
#define Z80DMA_STATUS_EOB       (1 << 4)   // End of block
#define Z80DMA_STATUS_BUSY      (1 << 7)   // DMA in progress

/* DMA controller state */
typedef struct {
    z80dma_port_t port_a;       // Port A configuration
    z80dma_port_t port_b;       // Port B configuration

    uint8_t wr[7];              // Write registers WR0-WR6
    uint8_t status;             // Status register
    uint8_t int_vector;         // Interrupt vector
    uint8_t pulse_control;      // Pulse control register
    uint8_t mask_byte;          // Mask byte for search
    uint8_t match_byte;         // Match byte for search

    z80dma_state_t state;       // Current state
    z80dma_transfer_mode_t mode;// Transfer mode
    uint8_t int_state;          // Interrupt state machine

    bool enabled;               // DMA enabled
    bool direction_ab;          // true=A->B, false=B->A
    bool search_mode;           // Search/match enabled
    bool auto_restart;          // Auto-restart on end of block
    bool interrupt_enable;      // Interrupt on end of block
    bool interrupt_on_eob;      // WR4 interrupt condition
    bool interrupt_on_ready;    // WR4 interrupt condition
    bool ready_active_high;     // WR5 RDY polarity
    bool force_ready;           // WR6 FORCE READY command
    bool wait_enabled;          // WR5 CE/WAIT multiplex selection
    bool stop_on_match;         // WR3 search control
    bool status_affects_vector; // WR4 interrupt-vector option
    bool enable_after_reti;     // WR6 command B7

    uint8_t data_latch;         // Data being transferred
    uint8_t cmd_buffer[8];      // Associated-register queue
    uint8_t cmd_bytes_needed;   // Queue length
    uint8_t cmd_bytes_received; // Queue cursor
    uint8_t compat_state;       // Kept for source compatibility; always zero
    uint16_t programmed_length; // WR0 N-1 block length
    uint16_t byte_counter;      // RR1/RR2, increments after each read cycle
    uint32_t bytes_remaining;   // 1..65536 after LOAD
    uint8_t byte_gap_ticks;     // CPU clocks yielded after a byte-mode transfer
    uint8_t read_mask;          // WR6 read-register selection
    uint8_t read_index;
    uint8_t read_count;
    bool ready;                 // Last interpreted RDY level for RR0
    bool operation_occurred;    // RR0 bit 0

    uint64_t pins;              // Last pin state
} z80dma_t;

/* Initialize DMA */
uint64_t z80dma_init(z80dma_t *dma);

/* Reset DMA */
void z80dma_reset(z80dma_t *dma);

/* Tick DMA (call every system clock) */
uint64_t z80dma_tick(z80dma_t *dma, uint64_t pins);

/*
    Clock only the interrupt daisy-chain section of the chip.  Board models
    use this when a CPU core exposes the interrupt-acknowledge bus cycle at a
    different point from its normal peripheral tick.  The vector is still
    selected and driven by the DMA through M1/IORQ/IEI pins; callers must not
    inspect or edit the DMA's interrupt state.
*/
uint64_t z80dma_daisychain(z80dma_t *dma, uint64_t pins);

/* Write to DMA register (from CPU) */
uint64_t z80dma_write(z80dma_t *dma, uint8_t data);

/* Read from DMA status (from CPU) */
uint8_t z80dma_read(z80dma_t *dma);

#ifdef __cplusplus
}
#endif

/*--- IMPLEMENTATION ---*/
#ifdef CHIPS_IMPL
#ifndef CHIPS_ASSERT
    #include <assert.h>
    #define CHIPS_ASSERT(c) assert(c)
#endif

uint64_t z80dma_init(z80dma_t *dma)
{
    CHIPS_ASSERT(dma);
    memset(dma, 0, sizeof(z80dma_t));
    z80dma_reset(dma);
    return dma->pins;
}

void z80dma_reset(z80dma_t *dma)
{
    CHIPS_ASSERT(dma);
    memset(dma, 0, sizeof(*dma));
    dma->state = Z80DMA_STATE_IDLE;
    dma->mode = Z80DMA_MODE_BYTE;
    dma->direction_ab = true;
    /* Direct users historically presented RDY as an active-high virtual pin.
       A programmed WR5 overrides this before the DMA can be enabled. */
    dma->ready_active_high = true;
}

enum {
    Z80DMA_ASSOC_A_ADDR_LO = 1,
    Z80DMA_ASSOC_A_ADDR_HI,
    Z80DMA_ASSOC_LENGTH_LO,
    Z80DMA_ASSOC_LENGTH_HI,
    Z80DMA_ASSOC_A_TIMING,
    Z80DMA_ASSOC_B_TIMING,
    Z80DMA_ASSOC_MASK,
    Z80DMA_ASSOC_MATCH,
    Z80DMA_ASSOC_B_ADDR_LO,
    Z80DMA_ASSOC_B_ADDR_HI,
    Z80DMA_ASSOC_INTERRUPT,
    Z80DMA_ASSOC_PULSE,
    Z80DMA_ASSOC_VECTOR,
    Z80DMA_ASSOC_READ_MASK
};

static void _z80dma_disable(z80dma_t *dma)
{
    dma->enabled = false;
    dma->status &= (uint8_t)~Z80DMA_STATUS_BUSY;
    dma->state = Z80DMA_STATE_IDLE;
    dma->byte_gap_ticks = 0;
    dma->pins &= ~Z80DMA_BUSREQ;
}

static void _z80dma_queue(z80dma_t *dma, uint8_t assoc)
{
    CHIPS_ASSERT(dma->cmd_bytes_needed < sizeof(dma->cmd_buffer));
    dma->cmd_buffer[dma->cmd_bytes_needed++] = assoc;
}

static void _z80dma_begin_queue(z80dma_t *dma)
{
    dma->cmd_bytes_needed = 0;
    dma->cmd_bytes_received = 0;
}

static void _z80dma_mirror_length(z80dma_t *dma, uint32_t count)
{
    const uint16_t visible = (uint16_t)count;
    dma->port_a.block_length = visible;
    dma->port_b.block_length = visible;
}

static void _z80dma_update_start_length(z80dma_t *dma)
{
    const uint16_t count = (uint16_t)(dma->programmed_length + 1u);
    dma->port_a.start_length = count;
    dma->port_b.start_length = count;
}

static void _z80dma_load(z80dma_t *dma)
{
    dma->port_a.address = dma->port_a.start_address;
    dma->port_b.address = dma->port_b.start_address;
    dma->bytes_remaining = (uint32_t)dma->programmed_length + 1u;
    dma->byte_counter = 0;
    _z80dma_mirror_length(dma, dma->bytes_remaining);
    dma->status &= (uint8_t)~(Z80DMA_STATUS_EOB | Z80DMA_STATUS_MATCH);
    dma->force_ready = false;
    dma->state = Z80DMA_STATE_IDLE;
}

static void _z80dma_decode_address_mode(z80dma_port_t *port, uint8_t data)
{
    const uint8_t mode = (uint8_t)((data >> 4) & 3u);
    port->decrement = mode == 0;
    port->increment = mode == 1;
}

static void _z80dma_associated_write(z80dma_t *dma, uint8_t assoc, uint8_t data)
{
    switch (assoc) {
        case Z80DMA_ASSOC_A_ADDR_LO:
            dma->port_a.start_address = (uint16_t)((dma->port_a.start_address & 0xFF00u) | data);
            break;
        case Z80DMA_ASSOC_A_ADDR_HI:
            dma->port_a.start_address = (uint16_t)((dma->port_a.start_address & 0x00FFu) | ((uint16_t)data << 8));
            break;
        case Z80DMA_ASSOC_LENGTH_LO:
            dma->programmed_length = (uint16_t)((dma->programmed_length & 0xFF00u) | data);
            _z80dma_update_start_length(dma);
            break;
        case Z80DMA_ASSOC_LENGTH_HI:
            dma->programmed_length = (uint16_t)((dma->programmed_length & 0x00FFu) | ((uint16_t)data << 8));
            _z80dma_update_start_length(dma);
            break;
        case Z80DMA_ASSOC_A_TIMING:
            dma->port_a.timing = data;
            break;
        case Z80DMA_ASSOC_B_TIMING:
            dma->port_b.timing = data;
            break;
        case Z80DMA_ASSOC_MASK:
            dma->mask_byte = data;
            break;
        case Z80DMA_ASSOC_MATCH:
            dma->match_byte = data;
            break;
        case Z80DMA_ASSOC_B_ADDR_LO:
            dma->port_b.start_address = (uint16_t)((dma->port_b.start_address & 0xFF00u) | data);
            break;
        case Z80DMA_ASSOC_B_ADDR_HI:
            dma->port_b.start_address = (uint16_t)((dma->port_b.start_address & 0x00FFu) | ((uint16_t)data << 8));
            break;
        case Z80DMA_ASSOC_INTERRUPT:
            dma->interrupt_on_ready = (data & 0x40u) != 0;
            dma->status_affects_vector = (data & 0x20u) != 0;
            dma->interrupt_on_eob = (data & 0x02u) != 0;
            if (data & 0x08u)
                _z80dma_queue(dma, Z80DMA_ASSOC_PULSE);
            if (data & 0x10u)
                _z80dma_queue(dma, Z80DMA_ASSOC_VECTOR);
            break;
        case Z80DMA_ASSOC_PULSE:
            dma->pulse_control = data;
            break;
        case Z80DMA_ASSOC_VECTOR:
            dma->int_vector = data;
            break;
        case Z80DMA_ASSOC_READ_MASK:
            dma->read_mask = data;
            break;
        default:
            CHIPS_ASSERT(false);
            break;
    }
}

static void _z80dma_decode_wr0(z80dma_t *dma, uint8_t data)
{
    dma->wr[0] = data;
    dma->direction_ab = (data & 0x04u) != 0;
    dma->search_mode = (data & 0x03u) != 0x01u;
    _z80dma_begin_queue(dma);
    if (data & 0x08u) _z80dma_queue(dma, Z80DMA_ASSOC_A_ADDR_LO);
    if (data & 0x10u) _z80dma_queue(dma, Z80DMA_ASSOC_A_ADDR_HI);
    if (data & 0x20u) _z80dma_queue(dma, Z80DMA_ASSOC_LENGTH_LO);
    if (data & 0x40u) _z80dma_queue(dma, Z80DMA_ASSOC_LENGTH_HI);
}

static void _z80dma_decode_wr1(z80dma_t *dma, uint8_t data)
{
    dma->wr[1] = data;
    dma->port_a.is_memory = (data & 0x08u) == 0;
    _z80dma_decode_address_mode(&dma->port_a, data);
    _z80dma_begin_queue(dma);
    if (data & 0x40u) _z80dma_queue(dma, Z80DMA_ASSOC_A_TIMING);
}

static void _z80dma_decode_wr2(z80dma_t *dma, uint8_t data)
{
    dma->wr[2] = data;
    dma->port_b.is_memory = (data & 0x08u) == 0;
    _z80dma_decode_address_mode(&dma->port_b, data);
    _z80dma_begin_queue(dma);
    if (data & 0x40u) _z80dma_queue(dma, Z80DMA_ASSOC_B_TIMING);
}

static void _z80dma_decode_wr3(z80dma_t *dma, uint8_t data)
{
    dma->wr[3] = data;
    dma->stop_on_match = (data & 0x04u) != 0;
    dma->interrupt_enable = (data & 0x20u) != 0;
    _z80dma_begin_queue(dma);
    if (data & 0x08u) _z80dma_queue(dma, Z80DMA_ASSOC_MASK);
    if (data & 0x10u) _z80dma_queue(dma, Z80DMA_ASSOC_MATCH);
    if (data & 0x40u) {
        dma->enabled = true;
        dma->status |= Z80DMA_STATUS_BUSY;
    }
}

static void _z80dma_decode_wr4(z80dma_t *dma, uint8_t data)
{
    dma->wr[4] = data;
    switch ((data >> 5) & 3u) {
        case 0: dma->mode = Z80DMA_MODE_BYTE; break;
        case 1: dma->mode = Z80DMA_MODE_CONTINUOUS; break;
        case 2: dma->mode = Z80DMA_MODE_BURST; break;
        default: dma->mode = Z80DMA_MODE_BYTE; break;
    }
    _z80dma_begin_queue(dma);
    if (data & 0x04u) _z80dma_queue(dma, Z80DMA_ASSOC_B_ADDR_LO);
    if (data & 0x08u) _z80dma_queue(dma, Z80DMA_ASSOC_B_ADDR_HI);
    if (data & 0x10u) _z80dma_queue(dma, Z80DMA_ASSOC_INTERRUPT);
}

static void _z80dma_decode_wr5(z80dma_t *dma, uint8_t data)
{
    dma->wr[5] = data;
    dma->auto_restart = (data & 0x20u) != 0;
    dma->wait_enabled = (data & 0x10u) != 0;
    dma->ready_active_high = (data & 0x08u) != 0;
    _z80dma_begin_queue(dma);
}

static void _z80dma_decode_wr6(z80dma_t *dma, uint8_t data)
{
    dma->wr[6] = data;
    if (data != 0x87u)
        _z80dma_disable(dma);

    switch (data) {
        case 0xC3: z80dma_reset(dma); break;
        case 0xC7: dma->port_a.timing = 0; break;
        case 0xCB: dma->port_b.timing = 0; break;
        case 0xCF: _z80dma_load(dma); break;
        case 0xD3:
            dma->bytes_remaining = (uint32_t)dma->programmed_length + 1u;
            dma->byte_counter = 0;
            _z80dma_mirror_length(dma, dma->bytes_remaining);
            dma->status &= (uint8_t)~Z80DMA_STATUS_EOB;
            break;
        case 0xAF: dma->interrupt_enable = false; break;
        case 0xAB: dma->interrupt_enable = true; break;
        case 0xA3:
            dma->interrupt_enable = false;
            dma->int_state = 0;
            break;
        case 0xB7: dma->enable_after_reti = true; break;
        case 0xBF:
            dma->read_mask = 0x01;
            dma->read_index = 0;
            dma->read_count = 1;
            break;
        case 0x8B: dma->status &= (uint8_t)~(Z80DMA_STATUS_MATCH | Z80DMA_STATUS_EOB); break;
        case 0xA7:
            dma->read_index = 0;
            dma->read_count = 7;
            break;
        case 0xB3: dma->force_ready = true; break;
        case 0x87:
            dma->enabled = true;
            dma->status |= Z80DMA_STATUS_BUSY;
            dma->state = Z80DMA_STATE_IDLE;
            break;
        case 0x83: break;
        case 0xBB:
            _z80dma_begin_queue(dma);
            _z80dma_queue(dma, Z80DMA_ASSOC_READ_MASK);
            break;
        default: break;
    }
}

uint64_t z80dma_write(z80dma_t *dma, uint8_t data)
{
    CHIPS_ASSERT(dma);

    if (dma->cmd_bytes_received < dma->cmd_bytes_needed) {
        const uint8_t assoc = dma->cmd_buffer[dma->cmd_bytes_received++];
        _z80dma_associated_write(dma, assoc, data);
        if (dma->cmd_bytes_received == dma->cmd_bytes_needed)
            _z80dma_begin_queue(dma);
        return dma->pins;
    }

    /* Every control byte other than ENABLE DMA disables bus requests. WR3's
       enable bit is applied again by its decoder below. */
    if (data != 0x87u)
        _z80dma_disable(dma);

    if ((data & 0x83u) == 0x83u) {
        _z80dma_decode_wr6(dma, data);
    } else if ((data & 0xC7u) == 0x82u) {
        _z80dma_decode_wr5(dma, data);
    } else if ((data & 0x83u) == 0x81u) {
        _z80dma_decode_wr4(dma, data);
    } else if ((data & 0x83u) == 0x80u) {
        _z80dma_decode_wr3(dma, data);
    } else if ((data & 0x87u) == 0x04u) {
        _z80dma_decode_wr1(dma, data);
    } else if ((data & 0x87u) == 0x00u) {
        _z80dma_decode_wr2(dma, data);
    } else if (((data & 0x80u) == 0) && ((data & 0x03u) != 0)) {
        _z80dma_decode_wr0(dma, data);
    }
    return dma->pins;
}

static uint8_t _z80dma_status_byte(const z80dma_t *dma)
{
    uint8_t value = 0;
    if (dma->operation_occurred) value |= 0x01u;
    if (dma->ready) value |= 0x02u;
    if ((dma->int_state & (Z80DMA_INT_NEEDED | Z80DMA_INT_REQUESTED)) == 0)
        value |= 0x08u;
    if ((dma->status & Z80DMA_STATUS_MATCH) == 0) value |= 0x10u;
    if ((dma->status & Z80DMA_STATUS_EOB) == 0) value |= 0x20u;
    return value;
}

static uint8_t _z80dma_read_register(const z80dma_t *dma, uint8_t bit)
{
    switch (bit) {
        case 0: return _z80dma_status_byte(dma);
        case 1: return (uint8_t)(dma->byte_counter & 0xFFu);
        case 2: return (uint8_t)(dma->byte_counter >> 8);
        case 3: return (uint8_t)(dma->port_a.address & 0xFFu);
        case 4: return (uint8_t)(dma->port_a.address >> 8);
        case 5: return (uint8_t)(dma->port_b.address & 0xFFu);
        case 6: return (uint8_t)(dma->port_b.address >> 8);
        default: return 0xFF;
    }
}

uint8_t z80dma_read(z80dma_t *dma)
{
    CHIPS_ASSERT(dma);
    if (dma->read_count != 0) {
        while (dma->read_index < 7u) {
            const uint8_t bit = dma->read_index++;
            if (dma->read_mask & (1u << bit))
                return _z80dma_read_register(dma, bit);
        }
        dma->read_count = 0;
    }
    return _z80dma_status_byte(dma);
}

static bool _z80dma_ready(const z80dma_t *dma, uint64_t pins)
{
    const bool pin_high = (pins & Z80DMA_RDY) != 0;
    return dma->force_ready || (pin_high == dma->ready_active_high);
}

static void _z80dma_ensure_remaining(z80dma_t *dma)
{
    if (dma->bytes_remaining == 0 &&
        (dma->port_a.block_length != 0 || dma->port_b.block_length != 0)) {
        dma->bytes_remaining = dma->port_a.block_length != 0
            ? dma->port_a.block_length : dma->port_b.block_length;
        _z80dma_mirror_length(dma, dma->bytes_remaining);
    }
}

static void _z80dma_advance_address(z80dma_port_t *port)
{
    if (port->increment)
        ++port->address;
    else if (port->decrement)
        --port->address;
}

static uint64_t _z80dma_finish_byte(z80dma_t *dma, uint64_t pins, bool ready)
{
    CHIPS_ASSERT(dma->bytes_remaining != 0);
    --dma->bytes_remaining;
    _z80dma_mirror_length(dma, dma->bytes_remaining);

    if (dma->bytes_remaining == 0) {
        dma->status |= Z80DMA_STATUS_EOB;
        if (dma->interrupt_enable && dma->interrupt_on_eob)
            dma->int_state |= Z80DMA_INT_NEEDED;
        if (dma->auto_restart) {
            _z80dma_load(dma);
            dma->enabled = true;
            dma->status |= Z80DMA_STATUS_BUSY;
        } else {
            _z80dma_disable(dma);
            pins &= ~Z80DMA_BUSREQ;
            return pins;
        }
    }

    if (dma->mode == Z80DMA_MODE_BYTE) {
        dma->state = Z80DMA_STATE_IDLE;
        dma->byte_gap_ticks = 4;
        pins &= ~Z80DMA_BUSREQ;
    } else if (dma->mode == Z80DMA_MODE_BURST && !ready) {
        dma->state = Z80DMA_STATE_IDLE;
        pins &= ~Z80DMA_BUSREQ;
    } else if (dma->mode == Z80DMA_MODE_CONTINUOUS && !ready) {
        dma->state = Z80DMA_STATE_WAIT_READY;
        pins |= Z80DMA_BUSREQ;
    } else {
        dma->state = Z80DMA_STATE_READ;
        pins |= Z80DMA_BUSREQ;
    }
    return pins;
}

static uint64_t _z80dma_transfer_byte(z80dma_t *dma, uint64_t pins, bool ready)
{
    z80dma_port_t *src = dma->direction_ab ? &dma->port_a : &dma->port_b;
    z80dma_port_t *dst = dma->direction_ab ? &dma->port_b : &dma->port_a;

    if (dma->state == Z80DMA_STATE_READ) {
        Z80DMA_SET_ADDR(pins, src->address);
        pins |= (src->is_memory ? Z80DMA_MREQ : Z80DMA_IORQ) | Z80DMA_RD;
        dma->state = Z80DMA_STATE_READ_LATCH;
    } else if (dma->state == Z80DMA_STATE_READ_LATCH) {
        dma->data_latch = Z80DMA_GET_DATA(pins);
        ++dma->byte_counter;
        _z80dma_advance_address(src);
        dma->state = Z80DMA_STATE_WRITE;
    } else if (dma->state == Z80DMA_STATE_WRITE) {
        Z80DMA_SET_ADDR(pins, dst->address);
        Z80DMA_SET_DATA(pins, dma->data_latch);
        pins |= (dst->is_memory ? Z80DMA_MREQ : Z80DMA_IORQ) | Z80DMA_WR;
        _z80dma_advance_address(dst);
        pins = _z80dma_finish_byte(dma, pins, ready);
    }
    return pins;
}

/* Handle interrupt daisy chain */
static uint64_t _z80dma_int(z80dma_t *dma, uint64_t pins)
{
    if ((pins & Z80DMA_RETI) && (dma->int_state & Z80DMA_INT_SERVICED)) {
        dma->int_state &= ~Z80DMA_INT_SERVICED;
        if (dma->enable_after_reti) {
            dma->interrupt_enable = true;
            dma->enable_after_reti = false;
        }
        pins &= ~Z80DMA_RETI;
    }

    // Interrupt daisy chain
    if ((dma->int_state != 0) && (pins & Z80DMA_IEIO)) {
        pins &= ~Z80DMA_IEIO;

        /* INT is level-held from request until acknowledge. NEEDED is only
           the one-time promotion state; REQUESTED must continue driving INT
           on every intervening CPU cycle. */
        if (dma->int_state & (Z80DMA_INT_NEEDED | Z80DMA_INT_REQUESTED))
            pins |= Z80DMA_INT;
        if (dma->int_state & Z80DMA_INT_NEEDED) {
            dma->int_state = (dma->int_state & ~Z80DMA_INT_NEEDED) | Z80DMA_INT_REQUESTED;
        }

        /* A Z80 interrupt acknowledge asserts M1+IORQ without RD or WR.
           Requiring the complete bus signature matters when the DMA itself is
           bus master: a stale CPU M1 combined with the DMA's I/O read/write
           cycle must not consume the pending EOB interrupt. */
        if ((dma->int_state & Z80DMA_INT_REQUESTED) &&
            ((pins & (Z80DMA_IORQ | Z80DMA_M1 | Z80DMA_RD | Z80DMA_WR)) ==
             (Z80DMA_IORQ | Z80DMA_M1))) {
            Z80DMA_SET_DATA(pins, dma->int_vector);
            dma->int_state = (dma->int_state & ~Z80DMA_INT_REQUESTED) | Z80DMA_INT_SERVICED;
            pins &= ~Z80DMA_INT;
        }
    }

    return pins;
}

uint64_t z80dma_daisychain(z80dma_t *dma, uint64_t pins)
{
    CHIPS_ASSERT(dma);
    pins = _z80dma_int(dma, pins);
    dma->pins = pins;
    return pins;
}

/* Main tick function */
uint64_t z80dma_tick(z80dma_t *dma, uint64_t pins)
{
    CHIPS_ASSERT(dma);

    /* Clear stale DMA bus strobes unless a transfer phase asserts them again
       below. Without this, the host can keep seeing old RD/WR cycles after the
       DMA has already gone idle, which in turn causes repeated phantom writes. */
    pins &= ~(Z80DMA_MREQ | Z80DMA_IORQ | Z80DMA_RD | Z80DMA_WR);

    // Handle reset
    if (pins & Z80DMA_RESET) {
        z80dma_reset(dma);
        return pins;
    }

    // Handle CPU I/O access to DMA controller
    if ((pins & (Z80DMA_CE | Z80DMA_IORQ | Z80DMA_M1)) == (Z80DMA_CE | Z80DMA_IORQ)) {
        if (pins & Z80DMA_WR) {
            uint8_t data = Z80DMA_GET_DATA(pins);
            pins = z80dma_write(dma, data);
        } else if (pins & Z80DMA_RD) {
            uint8_t data = z80dma_read(dma);
            Z80DMA_SET_DATA(pins, data);
        }
        dma->pins = pins;
        return pins;
    }

    if (!dma->enabled) {
        pins &= ~Z80DMA_BUSREQ;
        pins = z80dma_daisychain(dma, pins);
        dma->pins = pins;
        return pins;
    }

    _z80dma_ensure_remaining(dma);
    const bool ready = _z80dma_ready(dma, pins);
    dma->ready = ready;

    /* Byte mode must allow the CPU at least one complete machine cycle after
       every transfer. Four clocks are the shortest Z80 machine cycle. */
    if (dma->byte_gap_ticks != 0) {
        --dma->byte_gap_ticks;
        pins &= ~Z80DMA_BUSREQ;
        pins = z80dma_daisychain(dma, pins);
        dma->pins = pins;
        return pins;
    }

    if (dma->state == Z80DMA_STATE_IDLE) {
        pins &= ~Z80DMA_BUSREQ;
        if (ready && dma->bytes_remaining != 0) {
            pins |= Z80DMA_BUSREQ;
            dma->state = Z80DMA_STATE_WAIT_BUS;
            dma->operation_occurred = true;
        }
    }

    if (dma->state == Z80DMA_STATE_WAIT_BUS) {
        pins |= Z80DMA_BUSREQ;
        if (!ready) {
            dma->state = Z80DMA_STATE_IDLE;
            pins &= ~Z80DMA_BUSREQ;
            dma->pins = pins;
            return pins;
        }
        if (pins & Z80DMA_BUSACK) {
            dma->state = Z80DMA_STATE_READ;
            dma->pins = pins;
            return pins;
        }
    }

    if (dma->state == Z80DMA_STATE_WAIT_READY) {
        pins |= Z80DMA_BUSREQ;
        if ((pins & Z80DMA_BUSACK) && ready)
            dma->state = Z80DMA_STATE_READ;
    }

    if ((pins & Z80DMA_BUSACK) &&
        (((dma->state == Z80DMA_STATE_READ) && ready) ||
         dma->state == Z80DMA_STATE_READ_LATCH ||
         dma->state == Z80DMA_STATE_WRITE)) {
        pins |= Z80DMA_BUSREQ;
        pins = _z80dma_transfer_byte(dma, pins, ready);
    }

    // Handle interrupts
    pins = z80dma_daisychain(dma, pins);

    dma->pins = pins;
    return pins;
}

#endif // CHIPS_IMPL
