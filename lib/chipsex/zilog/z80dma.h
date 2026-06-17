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
    - Search and match operations
    - Interrupt on end of block
    - Full BUSREQ/BUSACK handshake
    - Interrupt daisy chain protocol

    ## Not Implemented (yet):

    - Ready signal timing (WAIT states)
    - Precise cycle-by-cycle timing
    - DMA request pins (RDY A/B)

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
    pins = z80dma_write(&dma, 0x10);  // Port B address

    pins = z80dma_write(&dma, 0xCF);  // WR4: Continuous mode, enable DMA
    pins = z80dma_write(&dma, 0x87);  // WR6: Load, enable after block
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
    Z80DMA_STATE_READ,          // Reading from source
    Z80DMA_STATE_WRITE,         // Writing to destination
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

    uint8_t data_latch;         // Data being transferred
    uint8_t cmd_buffer[6];      // Multi-byte command buffer
    uint8_t cmd_bytes_needed;   // Bytes still needed for current command
    uint8_t cmd_bytes_received; // Bytes received so far
    uint8_t compat_state;       // Boot-ROM compatibility parser state

    uint64_t pins;              // Last pin state
} z80dma_t;

/* Initialize DMA */
uint64_t z80dma_init(z80dma_t *dma);

/* Reset DMA */
void z80dma_reset(z80dma_t *dma);

/* Tick DMA (call every system clock) */
uint64_t z80dma_tick(z80dma_t *dma, uint64_t pins);

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

    // Clear ports
    memset(&dma->port_a, 0, sizeof(z80dma_port_t));
    memset(&dma->port_b, 0, sizeof(z80dma_port_t));

    // Clear registers
    memset(dma->wr, 0, sizeof(dma->wr));
    dma->status = 0;
    dma->int_vector = 0;

    // Reset state
    dma->state = Z80DMA_STATE_IDLE;
    dma->mode = Z80DMA_MODE_BYTE;
    dma->int_state = 0;
    dma->enabled = false;
    dma->direction_ab = true;
    dma->search_mode = false;
    dma->auto_restart = false;
    dma->interrupt_enable = false;

    dma->cmd_bytes_needed = 0;
    dma->cmd_bytes_received = 0;
    dma->compat_state = 0;
}

/* Decode WR0 - Base register */
static void _z80dma_decode_wr0(z80dma_t *dma, uint8_t data)
{
    dma->wr[0] = data;
    uint8_t base = (data >> 3) & 0x0F;

    // Bit 2: Transfer direction (0=B->A, 1=A->B)
    dma->direction_ab = (data & 0x04) != 0;

    // Bits 3-6: Base register group
    switch (base) {
        case 0: // WR0 only
            break;
        case 1: // Port A start address (2 bytes follow)
            dma->cmd_bytes_needed = 2;
            break;
        case 2: // Port A block length (2 bytes follow)
            dma->cmd_bytes_needed = 2;
            break;
        case 3: // Port A timing (1 byte follows)
            dma->cmd_bytes_needed = 1;
            break;
        case 4: // Port B start address (2 bytes follow)
            dma->cmd_bytes_needed = 2;
            break;
        case 5: // Port B block length (2 bytes follow)
            dma->cmd_bytes_needed = 2;
            break;
        case 6: // Port B timing (1 byte follows)
            dma->cmd_bytes_needed = 1;
            break;
        case 7: // Interrupt control (1 byte follows)
            dma->cmd_bytes_needed = 1;
            break;
        case 8: // Pulse control (1 byte follows)
            dma->cmd_bytes_needed = 1;
            break;
        case 9: // Interrupt vector (1 byte follows)
            dma->cmd_bytes_needed = 1;
            break;
    }

    dma->cmd_buffer[0] = data;
    dma->cmd_bytes_received = 1;
}

/* Complete multi-byte WR0 command */
static void _z80dma_complete_wr0(z80dma_t *dma)
{
    uint8_t base = (dma->cmd_buffer[0] >> 3) & 0x0F;

    switch (base) {
        case 1: // Port A start address
            dma->port_a.start_address = dma->cmd_buffer[1] | (dma->cmd_buffer[2] << 8);
            dma->port_a.address = dma->port_a.start_address;
            break;
        case 2: // Port A block length
            dma->port_a.start_length = dma->cmd_buffer[1] | (dma->cmd_buffer[2] << 8);
            dma->port_a.block_length = dma->port_a.start_length;
            break;
        case 3: // Port A timing
            dma->port_a.timing = dma->cmd_buffer[1];
            break;
        case 4: // Port B start address
            dma->port_b.start_address = dma->cmd_buffer[1] | (dma->cmd_buffer[2] << 8);
            dma->port_b.address = dma->port_b.start_address;
            break;
        case 5: // Port B block length
            dma->port_b.start_length = dma->cmd_buffer[1] | (dma->cmd_buffer[2] << 8);
            dma->port_b.block_length = dma->port_b.start_length;
            break;
        case 6: // Port B timing
            dma->port_b.timing = dma->cmd_buffer[1];
            break;
        case 7: // Interrupt control
            dma->interrupt_enable = (dma->cmd_buffer[1] & 0x20) != 0;
            break;
        case 8: // Pulse control
            dma->pulse_control = dma->cmd_buffer[1];
            break;
        case 9: // Interrupt vector
            dma->int_vector = dma->cmd_buffer[1];
            break;
    }
}

/* Decode WR1 - Port A mode */
static void _z80dma_decode_wr1(z80dma_t *dma, uint8_t data)
{
    dma->wr[1] = data;
    dma->port_a.is_memory = (data & 0x08) != 0;

    uint8_t addr_mode = data & 0x03;
    dma->port_a.increment = (addr_mode == 0);
    dma->port_a.decrement = (addr_mode == 1);
}

/* Decode WR2 - Port B mode */
static void _z80dma_decode_wr2(z80dma_t *dma, uint8_t data)
{
    dma->wr[2] = data;
    dma->port_b.is_memory = (data & 0x08) != 0;

    uint8_t addr_mode = data & 0x03;
    dma->port_b.increment = (addr_mode == 0);
    dma->port_b.decrement = (addr_mode == 1);
}

/* Decode WR3 - Mask byte (for search) */
static void _z80dma_decode_wr3(z80dma_t *dma, uint8_t data)
{
    dma->wr[3] = data;
    if (dma->cmd_bytes_needed > 0) {
        dma->cmd_buffer[dma->cmd_bytes_received++] = data;
        dma->cmd_bytes_needed--;
        if (dma->cmd_bytes_needed == 0) {
            // All bytes received, process command
            uint8_t cmd = (dma->cmd_buffer[0] >> 5) & 0x03;
            if (cmd == 1) { // Search mode
                dma->mask_byte = dma->cmd_buffer[1];
                dma->search_mode = true;
            }
        }
    } else {
        // Single byte command
        dma->cmd_buffer[0] = data;
        uint8_t cmd = (data >> 5) & 0x03;
        if (cmd == 1) { // Mask follows
            dma->cmd_bytes_needed = 1;
            dma->cmd_bytes_received = 1;
        }
    }
}

/* Decode WR4 - Mode/status control */
static void _z80dma_decode_wr4(z80dma_t *dma, uint8_t data)
{
    dma->wr[4] = data;

    // Bits 0-1: Transfer mode
    dma->mode = (z80dma_transfer_mode_t)(data & 0x03);

    // Bit 4: Auto restart
    dma->auto_restart = (data & 0x10) != 0;

    // Bit 6: Enable DMA (after programming)
    if (data & 0x40) {
        dma->enabled = true;
        dma->status |= Z80DMA_STATUS_BUSY;
    }

    // Bit 7: DMA enable (immediate)
    if (data & 0x80) {
        dma->enabled = false;
        dma->status &= ~Z80DMA_STATUS_BUSY;
    }
}

/* Decode WR5 - Ready/interrupt control */
static void _z80dma_decode_wr5(z80dma_t *dma, uint8_t data)
{
    dma->wr[5] = data;
    // Ready control bits - not fully implemented
}

/* Decode WR6 - Command register */
static void _z80dma_decode_wr6(z80dma_t *dma, uint8_t data)
{
    dma->wr[6] = data;

    uint8_t cmd = (data >> 3) & 0x0F;

    switch (cmd) {
        case 0: // Reset
            z80dma_reset(dma);
            break;
        case 1: // Reset port A timing
            dma->port_a.timing = 0;
            break;
        case 2: // Reset port B timing
            dma->port_b.timing = 0;
            break;
        case 3: // Load
            dma->port_a.block_length = dma->port_a.start_length;
            dma->port_b.block_length = dma->port_b.start_length;
            break;
        case 4: // Continue
            dma->enabled = true;
            dma->status |= Z80DMA_STATUS_BUSY;
            break;
        case 5: // Disable interrupts
            dma->interrupt_enable = false;
            break;
        case 6: // Enable interrupts
            dma->interrupt_enable = true;
            break;
        case 7: // Reset and disable interrupts
            dma->int_state = 0;
            dma->interrupt_enable = false;
            break;
        case 8: // Enable after EOP
            // Will enable when End Of Process occurs
            break;
        case 9: // Read status byte
            // Status will be read on next read cycle
            break;
        case 10: // Reinitialize status byte
            dma->status &= ~(Z80DMA_STATUS_MATCH | Z80DMA_STATUS_EOB);
            break;
        case 11: // Initiate read sequence
            // For reading counter/status
            break;
        case 12: // Force ready
            break;
        case 13: // Enable DMA
            dma->enabled = true;
            dma->status |= Z80DMA_STATUS_BUSY;
            break;
        case 14: // Disable DMA
            dma->enabled = false;
            dma->status &= ~Z80DMA_STATUS_BUSY;
            break;
        case 15: // Read mask follows
            dma->cmd_bytes_needed = 1;
            dma->cmd_bytes_received = 1;
            dma->cmd_buffer[0] = data;
            break;
    }
}

/* Write to DMA register */
uint64_t z80dma_write(z80dma_t *dma, uint8_t data)
{
    CHIPS_ASSERT(dma);

    /*
        Compatibility parser for the Partner boot ROM DMA sequences.

        The ROM programs the Z80 DMA with a small subset of commands using the
        byte stream:

            79 <A-addr lo> <A-addr hi> <len lo> <len hi>
            14 28
            95 <B-port>
            ...
            87

        The generic decoder below doesn't currently classify those bytes
        correctly, which leaves the DMA idle while the FDC waits in EXECUTE.
        Handle the boot-time subset explicitly so the DMA can move sector bytes
        from a fixed I/O port into RAM.
    */
    if (dma->compat_state != 0) {
        switch (dma->compat_state) {
            case 1:
                dma->cmd_buffer[1] = data;
                dma->compat_state = 2;
                return dma->pins;
            case 2:
                dma->cmd_buffer[2] = data;
                dma->compat_state = 3;
                return dma->pins;
            case 3:
                dma->cmd_buffer[3] = data;
                dma->compat_state = 4;
                return dma->pins;
            case 4: {
                dma->cmd_buffer[4] = data;
                dma->port_a.start_address = (uint16_t)(dma->cmd_buffer[1] | (dma->cmd_buffer[2] << 8));
                dma->port_a.address = dma->port_a.start_address;
                /*
                    The ROM programs the transfer count as N-1, so convert it to
                    an actual byte count for the simplified transfer loop.
                */
                uint16_t raw_len = (uint16_t)(dma->cmd_buffer[3] | (dma->cmd_buffer[4] << 8));
                dma->port_a.start_length = (uint16_t)(raw_len + 1);
                dma->port_a.block_length = dma->port_a.start_length;
                /*
                    The simplified transfer loop decrements the source port's
                    counter. Mirror the programmed block length into both ports
                    so B->A Partner boot transfers don't terminate immediately
                    with port B length still at zero.
                */
                dma->port_b.start_length = dma->port_a.start_length;
                dma->port_b.block_length = dma->port_b.start_length;
                dma->compat_state = 0;
                return dma->pins;
            }
            case 5:
                dma->port_b.start_address = data;
                dma->port_b.address = dma->port_b.start_address;
                dma->compat_state = 6;
                return dma->pins;
            case 6:
                /*
                    Partner boot blocks include a filler byte after the fixed
                    I/O port number before the port timing byte.
                */
                dma->compat_state = 7;
                return dma->pins;
            case 7:
                /*
                    The next byte is the fixed-I/O timing/control value (0x8A
                    in the ROM tables). Swallow it here so the generic decoder
                    doesn't reinterpret it as WR2 and accidentally switch port
                    B back to memory mode.
                */
                dma->port_b.timing = data;
                dma->port_b.is_memory = false;
                dma->compat_state = 0;
                return dma->pins;
            case 8:
                /*
                    Later Partner floppy loader code uses a shorter variant:

                        ... 28 85 <B-port> 8A CF 01 CF 87

                    so after the 0x85 marker the next byte is still the fixed
                    I/O port number, and the byte after that is already the
                    timing/control value.
                */
                dma->port_b.start_address = data;
                dma->port_b.address = dma->port_b.start_address;
                dma->compat_state = 9;
                return dma->pins;
            case 9:
                dma->port_b.timing = data;
                dma->port_b.is_memory = false;
                dma->compat_state = 0;
                return dma->pins;
            default:
                dma->compat_state = 0;
                break;
        }
    }

    switch (data) {
        case 0x05:
            z80dma_reset(dma);
            return dma->pins;
        case 0x79:
            dma->direction_ab = false;     // Port B (I/O) -> Port A (memory)
            dma->compat_state = 1;         // Expect A address + count
            return dma->pins;
        case 0x7D:
            // WR0 with bit 2 set: A->B direction (Port A memory -> Port B I/O).
            // Same address+count programming as 0x79; only the transfer
            // direction differs, so device writes (RAM -> FDC/SASI) work too.
            dma->direction_ab = true;      // Port A (memory) -> Port B (I/O)
            dma->compat_state = 1;         // Expect A address + count
            return dma->pins;
        case 0x14:
            dma->wr[1] = data;
            dma->port_a.is_memory = true;
            dma->port_a.increment = true;
            dma->port_a.decrement = false;
            return dma->pins;
        case 0x28:
            dma->wr[2] = data;
            return dma->pins;
        case 0x95:
            dma->wr[2] = data;
            dma->port_b.is_memory = false;
            dma->port_b.increment = false;
            dma->port_b.decrement = false;
            dma->compat_state = 5;         // Expect fixed I/O port + filler + timing
            return dma->pins;
        case 0x85:
            dma->wr[2] = data;
            dma->port_b.is_memory = false;
            dma->port_b.increment = false;
            dma->port_b.decrement = false;
            dma->compat_state = (data == 0x85) ? 8 : 5;
            return dma->pins;
        case 0xCF:
            dma->wr[4] = data;
            dma->mode = Z80DMA_MODE_CONTINUOUS;
            return dma->pins;
        case 0x87:
            dma->wr[6] = data;
            dma->port_a.address = dma->port_a.start_address;
            dma->port_a.block_length = dma->port_a.start_length;
            dma->port_b.address = dma->port_b.start_address;
            dma->enabled = true;
            dma->status |= Z80DMA_STATUS_BUSY;
            return dma->pins;
        case 0x8A:
            /* WR5 (wait/ready control) — swallow without corrupting port_b.is_memory.
               The generic decoder would mis-classify this as a WR2 byte and set
               port_b.is_memory=true, redirecting DMA reads from FDC I/O port 0xF1
               to RAM address 0xF1 instead. */
            return dma->pins;
        default:
            break;
    }

    // Handle multi-byte commands
    if (dma->cmd_bytes_needed > 0) {
        dma->cmd_buffer[dma->cmd_bytes_received++] = data;
        dma->cmd_bytes_needed--;

        if (dma->cmd_bytes_needed == 0) {
            // Command complete
            _z80dma_complete_wr0(dma);
        }
        return dma->pins;
    }

    // Decode register based on first bits
    uint8_t reg = (data >> 6) & 0x03;

    if ((data & 0x80) == 0) {
        // WR0 base register
        _z80dma_decode_wr0(dma, data);
    } else {
        // WR1-WR6
        switch (reg) {
            case 1: _z80dma_decode_wr1(dma, data); break;
            case 2: _z80dma_decode_wr2(dma, data); break;
            case 3: _z80dma_decode_wr3(dma, data); break;
        }

        if ((data & 0xC0) == 0xC0) {
            // WR4-WR6
            uint8_t subreg = (data >> 5) & 0x01;
            if (subreg == 0) {
                _z80dma_decode_wr4(dma, data);
            } else {
                uint8_t wr56 = (data >> 3) & 0x01;
                if (wr56 == 0) {
                    _z80dma_decode_wr5(dma, data);
                } else {
                    _z80dma_decode_wr6(dma, data);
                }
            }
        }
    }

    return dma->pins;
}

/* Read DMA status */
uint8_t z80dma_read(z80dma_t *dma)
{
    CHIPS_ASSERT(dma);

    // Return status byte
    uint8_t status = dma->status;

    // Add counter LSB in lower bits (simplified)
    status |= (dma->port_a.block_length & 0x7F);

    return status;
}

/* Perform one byte transfer */
static uint64_t _z80dma_transfer_byte(z80dma_t *dma, uint64_t pins)
{
    z80dma_port_t *src = dma->direction_ab ? &dma->port_a : &dma->port_b;
    z80dma_port_t *dst = dma->direction_ab ? &dma->port_b : &dma->port_a;

    // Read phase
    if (dma->state == Z80DMA_STATE_READ) {
        Z80DMA_SET_ADDR(pins, src->address);
        if (src->is_memory) {
            pins |= Z80DMA_MREQ | Z80DMA_RD;
        } else {
            pins |= Z80DMA_IORQ | Z80DMA_RD;
        }

        dma->data_latch = Z80DMA_GET_DATA(pins);

        // Update source address
        if (src->increment) {
            src->address++;
        } else if (src->decrement) {
            src->address--;
        }

        dma->state = Z80DMA_STATE_WRITE;
        return pins;
    }

    // Write phase
    if (dma->state == Z80DMA_STATE_WRITE) {
        pins &= ~(Z80DMA_MREQ | Z80DMA_IORQ | Z80DMA_RD);

        Z80DMA_SET_ADDR(pins, dst->address);
        Z80DMA_SET_DATA(pins, dma->data_latch);

        if (dst->is_memory) {
            pins |= Z80DMA_MREQ | Z80DMA_WR;
        } else {
            pins |= Z80DMA_IORQ | Z80DMA_WR;
        }

        // Update destination address
        if (dst->increment) {
            dst->address++;
        } else if (dst->decrement) {
            dst->address--;
        }

        // Decrement counter
        src->block_length--;

        // Check for end of block
        if (src->block_length == 0) {
            dma->status |= Z80DMA_STATUS_EOB;

            if (dma->auto_restart) {
                // Reload counters
                dma->port_a.address = dma->port_a.start_address;
                dma->port_b.address = dma->port_b.start_address;
                dma->port_a.block_length = dma->port_a.start_length;
                dma->port_b.block_length = dma->port_b.start_length;
            } else {
                dma->enabled = false;
                dma->status &= ~Z80DMA_STATUS_BUSY;
                pins &= ~Z80DMA_BUSREQ;
            }

            // Trigger interrupt if enabled
            if (dma->interrupt_enable) {
                dma->int_state |= Z80DMA_INT_NEEDED;
            }
        }

        dma->state = Z80DMA_STATE_IDLE;
        return pins;
    }

    return pins;
}

/* Handle interrupt daisy chain */
static uint64_t _z80dma_int(z80dma_t *dma, uint64_t pins)
{
    // RETI handling
    if ((pins & Z80DMA_RETI) && (dma->int_state & Z80DMA_INT_SERVICED)) {
        dma->int_state &= ~Z80DMA_INT_SERVICED;
        pins &= ~Z80DMA_RETI;
    }

    // Interrupt daisy chain
    if ((dma->int_state != 0) && (pins & Z80DMA_IEIO)) {
        pins &= ~Z80DMA_IEIO;

        if (dma->int_state & Z80DMA_INT_NEEDED) {
            pins |= Z80DMA_INT;
            dma->int_state = (dma->int_state & ~Z80DMA_INT_NEEDED) | Z80DMA_INT_REQUESTED;
        }

        if ((dma->int_state & Z80DMA_INT_REQUESTED) &&
            ((pins & (Z80DMA_IORQ | Z80DMA_M1)) == (Z80DMA_IORQ | Z80DMA_M1))) {
            Z80DMA_SET_DATA(pins, dma->int_vector);
            dma->int_state = (dma->int_state & ~Z80DMA_INT_REQUESTED) | Z80DMA_INT_SERVICED;
            pins &= ~Z80DMA_INT;
        }
    }

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

    // If not enabled, nothing to do
    if (!dma->enabled) {
        pins = _z80dma_int(dma, pins);
        dma->pins = pins;
        return pins;
    }

    // When idle, don't hold the system bus hostage between transfers. The
    // Partner loader programs the DMA in continuous mode, but the FDC source
    // only becomes ready intermittently, so BUSREQ must be released while we
    // wait for the next byte/sector handshake.
    if (dma->state == Z80DMA_STATE_IDLE) {
        pins &= ~Z80DMA_BUSREQ;
        if ((pins & Z80DMA_RDY) && (dma->port_a.block_length > 0 || dma->port_b.block_length > 0)) {
            // Need to transfer, request bus
            pins |= Z80DMA_BUSREQ;
            dma->state = Z80DMA_STATE_WAIT_BUS;
        }
    }

    // Wait for bus grant
    if (dma->state == Z80DMA_STATE_WAIT_BUS) {
        if (!(pins & Z80DMA_RDY)) {
            dma->state = Z80DMA_STATE_IDLE;
            pins &= ~Z80DMA_BUSREQ;
            dma->pins = pins;
            return pins;
        }
        if ((pins & Z80DMA_BUSACK) && (pins & Z80DMA_RDY)) {
            // Bus granted, start transfer
            dma->state = Z80DMA_STATE_READ;
            dma->pins = pins;
            return pins;
        }
    }

    // Perform transfer if bus is granted
    if ((pins & Z80DMA_BUSACK) &&
        (((dma->state == Z80DMA_STATE_READ) && (pins & Z80DMA_RDY)) ||
         (dma->state == Z80DMA_STATE_WRITE))) {
        pins = _z80dma_transfer_byte(dma, pins);

        // In byte mode, release bus after each byte
        if (dma->mode == Z80DMA_MODE_BYTE && dma->state == Z80DMA_STATE_IDLE) {
            pins &= ~Z80DMA_BUSREQ;
        }
    }

    // Handle interrupts
    pins = _z80dma_int(dma, pins);

    dma->pins = pins;
    return pins;
}

#endif // CHIPS_IMPL
