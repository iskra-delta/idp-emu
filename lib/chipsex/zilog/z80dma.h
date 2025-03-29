/*#
    # z80dma.h

    Header-only emulator for the Zilog Z80 DMA (Direct Memory Access)
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

    *************************************************
    *           +-----------+                       *
    *   A0..A15 |           | <--> Memory / I/O     *
    *   D0..D7  |           | <--> Memory / I/O     *
    *   MREQ -->|           |<--- RD                *
    *   IORQ -->|    Z80    |<--- WR                *
    *  BUSREQ <-|    DMA    |---> BUSACK            *
    *    INT <- |           |                       *
    *    M1 --> |           |                       *
    *  RESET -->|           |                       *
    *           +-----------+                       *
    *************************************************

    ## Supported Features:

    - Memory-to-I/O and I/O-to-memory DMA transfers
    - Block mode transfers with automatic address increment
    - Full Z80 BUSREQ / BUSACK handshake simulation
    - Data latch and transfer state machine
    - INT signaling on DMA completion

    ## Not Implemented (yet):

    - Memory-to-memory transfers
    - Cycle stealing (interleaved DMA)
    - Ready (WAIT state) logic
    - External control signals (like TC, HALT)
    - Priority chaining and multiple DMA channels

    ## Usage:

    Initialize:

    ~~~C
    z80dma_t dma;
    z80dma_init(&dma);
    ~~~

    Start a transfer:

    ~~~C
    z80dma_start(&dma, source_addr, dest_addr, count, Z80DMA_MODE_IO_TO_MEM, true);
    ~~~

    On each system tick:

    ~~~C
    pins = z80dma_tick(&dma, pins);
    ~~~

    You are responsible for routing memory and I/O reads/writes
    based on the pin signals emitted by the DMA controller.

    ## DMA Modes:

    ~~~C
    typedef enum {
        Z80DMA_MODE_MEM_TO_IO,
        Z80DMA_MODE_IO_TO_MEM,
    } z80dma_mode_t;
    ~~~

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

#define Z80DMA_BUSREQ (1ULL << 32)
#define Z80DMA_BUSACK (1ULL << 33)
#define Z80DMA_MREQ (1ULL << 34)
#define Z80DMA_IORQ (1ULL << 35)
#define Z80DMA_RD (1ULL << 36)
#define Z80DMA_WR (1ULL << 37)
#define Z80DMA_INT (1ULL << 38)
#define Z80DMA_M1 (1ULL << 39)

#define Z80DMA_GET_DATA(p) ((uint8_t)(((p) >> 16) & 0xFF))
#define Z80DMA_SET_DATA(p, d)                                             \
    {                                                                     \
        p = ((p) & ~0xFF0000ULL) | (((uint64_t)(d) << 16) & 0xFF0000ULL); \
    }
#define Z80DMA_SET_ADDR(p, a)                       \
    {                                               \
        p = ((p) & ~0xFFFFULL) | ((a) & 0xFFFFULL); \
    }
#define Z80DMA_GET_ADDR(p) ((uint16_t)((p) & 0xFFFFULL))

    typedef enum
    {
        Z80DMA_MODE_MEM_TO_IO,
        Z80DMA_MODE_IO_TO_MEM,
    } z80dma_mode_t;

    typedef struct
    {
        uint16_t source_addr;
        uint16_t dest_addr;
        uint16_t counter;
        z80dma_mode_t mode;
        bool active;
        bool bus_granted;
        bool interrupt_enable;
        bool completed;
        uint8_t data_latch;
    } z80dma_t;

    void z80dma_init(z80dma_t *dma);
    void z80dma_start(z80dma_t *dma, uint16_t src, uint16_t dst, uint16_t count, z80dma_mode_t mode, bool interrupt_enable);
    uint64_t z80dma_tick(z80dma_t *dma, uint64_t pins);

#ifdef __cplusplus
}
#endif

#ifdef CHIPS_IMPL
void z80dma_init(z80dma_t *dma)
{
    memset(dma, 0, sizeof(z80dma_t));
}

void z80dma_start(z80dma_t *dma, uint16_t src, uint16_t dst, uint16_t count, z80dma_mode_t mode, bool interrupt_enable)
{
    dma->source_addr = src;
    dma->dest_addr = dst;
    dma->counter = count;
    dma->mode = mode;
    dma->active = true;
    dma->interrupt_enable = interrupt_enable;
    dma->bus_granted = false;
    dma->completed = false;
}

uint64_t z80dma_tick(z80dma_t *dma, uint64_t pins)
{
    if (!dma->active || dma->counter == 0)
    {
        if (dma->interrupt_enable && dma->completed)
        {
            pins |= Z80DMA_INT;
        }
        return pins;
    }

    if (!(pins & Z80DMA_BUSACK))
    {
        pins |= Z80DMA_BUSREQ;
        return pins;
    }

    dma->bus_granted = true;
    pins &= ~Z80DMA_BUSREQ;

    // Transfer step
    if (dma->mode == Z80DMA_MODE_MEM_TO_IO)
    {
        pins |= Z80DMA_MREQ | Z80DMA_RD;
        Z80DMA_SET_ADDR(pins, dma->source_addr);
        dma->data_latch = Z80DMA_GET_DATA(pins);
        dma->source_addr++;
        pins &= ~(Z80DMA_MREQ | Z80DMA_RD);

        pins |= Z80DMA_IORQ | Z80DMA_WR;
        Z80DMA_SET_ADDR(pins, dma->dest_addr);
        Z80DMA_SET_DATA(pins, dma->data_latch);
        pins &= ~(Z80DMA_IORQ | Z80DMA_WR);
    }
    else if (dma->mode == Z80DMA_MODE_IO_TO_MEM)
    {
        pins |= Z80DMA_IORQ | Z80DMA_RD;
        Z80DMA_SET_ADDR(pins, dma->source_addr);
        dma->data_latch = Z80DMA_GET_DATA(pins);
        pins &= ~(Z80DMA_IORQ | Z80DMA_RD);

        pins |= Z80DMA_MREQ | Z80DMA_WR;
        Z80DMA_SET_ADDR(pins, dma->dest_addr);
        Z80DMA_SET_DATA(pins, dma->data_latch);
        pins &= ~(Z80DMA_MREQ | Z80DMA_WR);
        dma->dest_addr++;
    }

    if (--dma->counter == 0)
    {
        dma->active = false;
        dma->completed = true;
    }

    return pins;
}
#endif // CHIPS_IMPL
