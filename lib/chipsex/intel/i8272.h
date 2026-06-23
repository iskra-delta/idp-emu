#pragma once
/*#
    # i8272.h

    Header-only emulator for the Intel 8272 Floppy Disk Controller
    (NEC uPD765A compatible) written in C.

    Do this:
    ~~~C
    #define CHIPS_IMPL
    ~~~
    before you include this file in *one* C or C++ file to create the
    implementation.

    Optionally provide the following macros with your own implementation
    ~~~C
    CHIPS_ASSERT(c)
    ~~~
        your own assert macro (default: assert(c))

    ## Emulated features:

    - Main Status Register (MSR) at port 0xF0
    - Data Register at port 0xF1
    - Command/Result phase state machine
    - Commands: SPECIFY, RECALIBRATE, SENSE INTERRUPT STATUS, READ DATA,
      WRITE DATA, FORMAT TRACK, SEEK
    - Per-drive state (track, head, motor)
    - Sector read/write via callbacks
    - CHIPS-style pin-level bus tick (`i8272_tick_pins`)

    ## Emulated Pins

    ***************************************
    *           +-----------+             *
    * D0..D7 <->|           |<-> A0       *
    *           |   i8272   |             *
    *   CS̅  --->|   FDC     |---> IRQ     *
    *   RD̅  --->|           |             *
    *   WR̅  --->|           |             *
    * RESET̅ --->|           |             *
    *           +-----------+             *
    ***************************************

    - D0..D7: bidirectional data bus
    - A0: register select (0=MSR/status, 1=data)
    - CS̅/RD̅/WR̅: active-low bus control
    - RESET̅: active-low reset
    - IRQ: INTRQ output

    ## Functions:
    ~~~C
    void i8272_init(i8272_t* fdc)
    ~~~
        Initializes a new 8272 FDC instance.

    ~~~C
    void i8272_reset(i8272_t* fdc)
    ~~~
        Puts the FDC into the reset state.

    ~~~C
    uint8_t i8272_read_status(i8272_t* fdc)
    ~~~
        Read the Main Status Register (port 0xF0).

    ~~~C
    uint8_t i8272_read_data(i8272_t* fdc)
    ~~~
        Read from the Data Register (port 0xF1).

    ~~~C
    void i8272_write_data(i8272_t* fdc, uint8_t data)
    ~~~
        Write to the Data Register (port 0xF1).
#*/
/*
    zlib/libpng license

    Copyright (c) 2025 Tomaz Stih
    This software is provided 'as-is', without any express or implied warranty.
    In no event will the authors be held liable for any damages arising from the
    use of this software.
    Permission is granted to anyone to use this software for any purpose,
    including commercial applications, and to alter it and redistribute it
    freely, subject to the following restrictions:
        1. The origin of this software must not be misrepresented; you must not
        claim that you wrote the original software. If you use this software in a
        product, an acknowledgment in the product documentation would be
        appreciated but is not required.
        2. Altered source versions must be plainly marked as such, and must not
        be misrepresented as being the original software.
        3. This notice may not be removed or altered from any source
        distribution.
*/
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*--- CHIPS-style bus helpers ---*/
#define I8272_GET_ADDR(p)      ((uint8_t)((p) & 0x03))
#define I8272_GET_DATA(p)      ((uint8_t)(((p) >> 16) & 0xFF))
#define I8272_SET_DATA(p, d)   { p = ((p) & ~0xFF0000ULL) | (((uint64_t)(d) << 16) & 0xFF0000ULL); }

/* Active-low bus control pins */
#define I8272_PIN_RD           (24)
#define I8272_PIN_WR           (25)
#define I8272_PIN_CS           (26)
#define I8272_PIN_RESET        (27)
#define I8272_PIN_IRQ          (28)  /* INTRQ output */

#define I8272_RD               (1ULL << I8272_PIN_RD)
#define I8272_WR               (1ULL << I8272_PIN_WR)
#define I8272_CS               (1ULL << I8272_PIN_CS)
#define I8272_RESET            (1ULL << I8272_PIN_RESET)
#define I8272_IRQ              (1ULL << I8272_PIN_IRQ)

/*--- MSR (Main Status Register) bits, read from port 0xF0 ---*/
#define I8272_MSR_RQM       (1 << 7)   /* Request for Master: 1=ready */
#define I8272_MSR_DIO       (1 << 6)   /* Data direction: 1=FDC->CPU, 0=CPU->FDC */
#define I8272_MSR_EXM       (1 << 5)   /* Execution mode */
#define I8272_MSR_CB        (1 << 4)   /* FDC busy */
#define I8272_MSR_D3B       (1 << 3)   /* Drive 3 busy */
#define I8272_MSR_D2B       (1 << 2)   /* Drive 2 busy */
#define I8272_MSR_D1B       (1 << 1)   /* Drive 1 busy */
#define I8272_MSR_D0B       (1 << 0)   /* Drive 0 busy */

/*--- ST0 (Status Register 0) bits ---*/
#define I8272_ST0_IC_MASK   (0xC0)     /* Interrupt Code mask */
#define I8272_ST0_IC_NT     (0x00)     /* Normal Termination */
#define I8272_ST0_IC_AT     (0x40)     /* Abnormal Termination */
#define I8272_ST0_IC_IC     (0x80)     /* Invalid Command */
#define I8272_ST0_IC_ATRDY  (0xC0)     /* Abnormal: drive not ready */
#define I8272_ST0_SE        (1 << 5)   /* Seek End */
#define I8272_ST0_EC        (1 << 4)   /* Equipment Check */
#define I8272_ST0_NR        (1 << 3)   /* Not Ready */
#define I8272_ST0_HD        (1 << 2)   /* Head Address */
#define I8272_ST0_US1       (1 << 1)   /* Unit Select 1 */
#define I8272_ST0_US0       (1 << 0)   /* Unit Select 0 */

/*--- ST1 (Status Register 1) bits ---*/
#define I8272_ST1_EN        (1 << 7)   /* End of Cylinder */
#define I8272_ST1_DE        (1 << 5)   /* Data Error (CRC) */
#define I8272_ST1_OR        (1 << 4)   /* Overrun */
#define I8272_ST1_ND        (1 << 2)   /* No Data */
#define I8272_ST1_NW        (1 << 1)   /* Not Writable */
#define I8272_ST1_MA        (1 << 0)   /* Missing Address Mark */

/*--- ST2 (Status Register 2) bits ---*/
#define I8272_ST2_CM        (1 << 6)   /* Control Mark */
#define I8272_ST2_DD        (1 << 5)   /* Data Error in Data Field */
#define I8272_ST2_WC        (1 << 4)   /* Wrong Cylinder */
#define I8272_ST2_BC        (1 << 1)   /* Bad Cylinder */
#define I8272_ST2_MD        (1 << 0)   /* Missing Data Address Mark */

/*--- Command codes (lower 5 bits) ---*/
#define I8272_CMD_SPECIFY           0x03
#define I8272_CMD_SENSE_DRIVE       0x04
#define I8272_CMD_RECALIBRATE       0x07
#define I8272_CMD_SENSE_INT         0x08
#define I8272_CMD_READ_DATA         0x06
#define I8272_CMD_WRITE_DATA        0x05
#define I8272_CMD_READ_ID           0x0A
#define I8272_CMD_FORMAT_TRACK      0x0D
#define I8272_CMD_SEEK              0x0F

/*--- FDC state machine phases ---*/
typedef enum {
    I8272_PHASE_IDLE = 0,
    I8272_PHASE_COMMAND,
    I8272_PHASE_EXECUTE,
    I8272_PHASE_RESULT,
} i8272_phase_t;

/*--- Max drives ---*/
#define I8272_MAX_DRIVES    4

/*--- Max sector buffer (512 bytes per sector) ---*/
#define I8272_SECTOR_SIZE   512

/*--- Per-drive state ---*/
typedef struct {
    uint8_t track;      /* present cylinder number (PCN) */
    uint8_t head;
    bool motor;
    bool ready;
} i8272_drive_t;

/*--- Callback: read a sector from disk image ---*/
typedef bool (*i8272_read_sector_cb)(int drive, int c, int h, int r, int n,
                                     uint8_t *buf, void *user);
/*--- Callback: write a sector to disk image ---*/
typedef bool (*i8272_write_sector_cb)(int drive, int c, int h, int r, int n,
                                      const uint8_t *buf, void *user);

/*--- Intel 8272 FDC state ---*/
typedef struct {
    /* phase state machine */
    i8272_phase_t phase;

    /* command buffer */
    uint8_t cmd[9];         /* max 9 bytes (READ DATA) */
    uint8_t cmd_len;        /* expected command length */
    uint8_t cmd_idx;        /* bytes received so far */
    uint8_t cmd_code;       /* decoded command (lower 5 bits of first byte) */

    /* result buffer */
    uint8_t result[7];      /* max 7 bytes */
    uint8_t result_len;     /* result bytes to return */
    uint8_t result_idx;     /* bytes read so far */

    /* status registers */
    uint8_t st0;
    uint8_t st1;
    uint8_t st2;
    uint8_t msr;            /* main status register */

    /* specify parameters */
    uint8_t srt;            /* step rate time */
    uint8_t hut;            /* head unload time */
    uint8_t hlt;            /* head load time */
    bool ndma;              /* non-DMA mode flag */

    /* drives */
    i8272_drive_t drive[I8272_MAX_DRIVES];

    /* interrupt/result bookkeeping */
    bool int_pending;
    bool irq_request;
    uint32_t irq_delay;
    bool irq_sets_sense;
    bool irq_enters_result;
    uint32_t exec_delay;
    bool exec_pending;

    /* sector data buffer for execution phase */
    uint8_t data_buf[I8272_SECTOR_SIZE];
    uint16_t data_len;
    uint16_t data_idx;
    /* last sector transfer request (debug visibility) */
    uint8_t last_us;
    uint8_t last_c;
    uint8_t last_h;
    uint8_t last_r;
    uint8_t last_n;
    bool last_read_ok;

    /* sector read/write callbacks */
    i8272_read_sector_cb read_sector;
    i8272_write_sector_cb write_sector;
    void *user_data;
} i8272_t;

/* Initialize FDC */
void i8272_init(i8272_t *fdc);
/* Reset FDC */
void i8272_reset(i8272_t *fdc);
/* Advance delayed interrupt bookkeeping */
void i8272_tick(i8272_t *fdc);
/* Pin-level tick for CHIPS-style bus integration */
uint64_t i8272_tick_pins(i8272_t *fdc, uint64_t pins);
/* Read Main Status Register (port 0xF0) */
uint8_t i8272_read_status(i8272_t *fdc);
/* Read Data Register (port 0xF1) */
uint8_t i8272_read_data(i8272_t *fdc);
/* Write Data Register (port 0xF1) */
void i8272_write_data(i8272_t *fdc, uint8_t data);

#ifdef __cplusplus
} /* extern "C" */
#endif

/*-- IMPLEMENTATION ----------------------------------------------------------*/
#ifdef CHIPS_IMPL
#include <stdio.h>
#include <string.h>
#ifndef CHIPS_ASSERT
    #include <assert.h>
    #define CHIPS_ASSERT(c) assert(c)
#endif

/*
    _i8272_cmd_length

    Returns expected total command length (including the command byte itself)
    for a given command code.
*/
static uint8_t _i8272_cmd_length(uint8_t cmd_code) {
    switch (cmd_code) {
        case I8272_CMD_SPECIFY:         return 3;   /* cmd + 2 params */
        case I8272_CMD_SENSE_DRIVE:     return 2;   /* cmd + 1 param */
        case I8272_CMD_RECALIBRATE:     return 2;   /* cmd + 1 param */
        case I8272_CMD_SENSE_INT:       return 1;   /* cmd only */
        case I8272_CMD_READ_DATA:       return 9;   /* cmd + 8 params */
        case I8272_CMD_WRITE_DATA:      return 9;   /* cmd + 8 params */
        case I8272_CMD_READ_ID:         return 2;   /* cmd + 1 param */
        case I8272_CMD_FORMAT_TRACK:    return 6;   /* cmd + 5 params */
        case I8272_CMD_SEEK:            return 3;   /* cmd + 2 params */
        default:                        return 1;   /* invalid: just the byte */
    }
}

static uint16_t _i8272_sector_size(uint8_t n) {
    uint16_t sector_size = (n == 0) ? 128 : (128 << n);
    if (sector_size > I8272_SECTOR_SIZE) {
        sector_size = I8272_SECTOR_SIZE;
    }
    return sector_size;
}

/*
    _i8272_cmd_name (for debug, not used at runtime but kept for reference)
*/

/*
    _i8272_enter_idle

    Transition to idle state, ready for next command.
*/
static void _i8272_enter_idle(i8272_t *fdc) {
    fdc->phase = I8272_PHASE_IDLE;
    fdc->cmd_idx = 0;
    fdc->cmd_len = 0;
    fdc->cmd_code = 0;
    fdc->result_idx = 0;
    fdc->result_len = 0;
    /* RQM=1, DIO=0 (CPU->FDC), not busy */
    fdc->msr = I8272_MSR_RQM;
}

static void _i8272_schedule_irq(i8272_t *fdc, uint32_t delay_ticks,
                                bool set_sense_pending, bool enter_result) {
    fdc->int_pending = false;
    fdc->irq_request = false;
    fdc->irq_delay = delay_ticks;
    fdc->irq_sets_sense = set_sense_pending;
    fdc->irq_enters_result = enter_result;
}

/*
    _i8272_enter_result

    Transition to result phase.
*/
static void _i8272_enter_result(i8272_t *fdc) {
    fdc->phase = I8272_PHASE_RESULT;
    fdc->result_idx = 0;
    /* RQM=1, DIO=1 (FDC->CPU), CB=1 */
    fdc->msr = I8272_MSR_RQM | I8272_MSR_DIO | I8272_MSR_CB;
}

static void _i8272_set_rw_result(i8272_t *fdc, uint8_t c, uint8_t h,
                                 uint8_t r, uint8_t n) {
    fdc->result[0] = fdc->st0;
    fdc->result[1] = fdc->st1;
    fdc->result[2] = fdc->st2;
    fdc->result[3] = c;
    fdc->result[4] = h;
    fdc->result[5] = r;
    fdc->result[6] = n;
    fdc->result_len = 7;
}

static bool _i8272_finalize_format_track(i8272_t *fdc, uint8_t us, uint8_t sc,
                                         uint8_t fill_byte) {
    if (!fdc->write_sector) {
        fdc->st0 = I8272_ST0_IC_AT;
        fdc->st1 = I8272_ST1_NW;
        fdc->st2 = 0;
        _i8272_set_rw_result(fdc, 0, 0, 0, 0);
        return false;
    }

    uint8_t sector_buf[I8272_SECTOR_SIZE];
    for (uint16_t i = 0; i < I8272_SECTOR_SIZE; i++) {
        sector_buf[i] = fill_byte;
    }

    for (uint8_t i = 0; i < sc; i++) {
        const uint8_t c = fdc->data_buf[i * 4 + 0];
        const uint8_t h = fdc->data_buf[i * 4 + 1];
        const uint8_t r = fdc->data_buf[i * 4 + 2];
        const uint8_t n = fdc->data_buf[i * 4 + 3];
        const uint16_t sector_size = _i8272_sector_size(n);

        fdc->last_c = c;
        fdc->last_h = h;
        fdc->last_r = r;
        fdc->last_n = n;

        if (!fdc->write_sector(us, c, h, r, n, sector_buf, fdc->user_data)) {
            fdc->st0 = fdc->drive[us].ready ? I8272_ST0_IC_AT : I8272_ST0_IC_ATRDY;
            fdc->st1 = I8272_ST1_NW;
            fdc->st2 = 0;
            _i8272_set_rw_result(fdc, c, h, r, n);
            (void)sector_size;
            return false;
        }
    }

    if (sc > 0) {
        const uint8_t last_base = (uint8_t)((sc - 1) * 4);
        _i8272_set_rw_result(
            fdc,
            fdc->data_buf[last_base + 0],
            fdc->data_buf[last_base + 1],
            fdc->data_buf[last_base + 2],
            fdc->data_buf[last_base + 3]);
    } else {
        _i8272_set_rw_result(fdc, 0, 0, 0, 0);
    }
    return true;
}

/*
    _i8272_exec_specify

    SPECIFY command: set step rate, head load/unload times.
    No interrupt generated. No result phase.
*/
static void _i8272_exec_specify(i8272_t *fdc) {
    fdc->srt = (fdc->cmd[1] >> 4) & 0x0F;
    fdc->hut = fdc->cmd[1] & 0x0F;
    fdc->hlt = (fdc->cmd[2] >> 1) & 0x7F;
    fdc->ndma = (fdc->cmd[2] & 0x01) != 0;
    _i8272_enter_idle(fdc);
}

/*
    _i8272_exec_recalibrate

    RECALIBRATE: seek to track 0 on the specified drive.
    Generates interrupt. No result phase (use SENSE INTERRUPT STATUS).
*/
static void _i8272_exec_recalibrate(i8272_t *fdc) {
    uint8_t us = fdc->cmd[1] & 0x03;
    fdc->drive[us].track = 0;
    fdc->st0 = I8272_ST0_SE | (us & 0x03);
    _i8272_enter_idle(fdc);
    /*
        Delay completion slightly so the ROM reaches its EI/HALT wait point
        before INTRQ appears.
    */
    _i8272_schedule_irq(fdc, 64, true, false);
}

/*
    _i8272_exec_sense_int

    SENSE INTERRUPT STATUS: returns ST0 + PCN.
    This is how the CPU reads the result of RECALIBRATE/SEEK.
*/
static void _i8272_exec_sense_int(i8272_t *fdc) {
    /*
        Reading the SENSE INTERRUPT STATUS result de-asserts INTRQ on the real
        i8272 (the CPU has acknowledged the seek/recal completion). Clear both
        int_pending and the INTRQ line here; leaving irq_request asserted would
        re-fire the driver's completion ISR as soon as interrupts are unmasked
        after a polled recalibrate.
    */
    fdc->irq_request = false;
    if (fdc->int_pending) {
        fdc->result[0] = fdc->st0;
        fdc->result[1] = fdc->drive[fdc->st0 & 0x03].track;
        fdc->result_len = 2;
        fdc->int_pending = false;
    } else {
        /* No interrupt pending: return invalid command code */
        fdc->result[0] = I8272_ST0_IC_IC;
        fdc->result_len = 1;
    }
    _i8272_enter_result(fdc);
}

/*
    _i8272_exec_sense_drive

    SENSE DRIVE STATUS: returns ST3.
*/
static void _i8272_exec_sense_drive(i8272_t *fdc) {
    uint8_t us = fdc->cmd[1] & 0x03;
    uint8_t hd = (fdc->cmd[1] >> 2) & 0x01;
    uint8_t st3 = us | (hd << 2);
    /* Track 0 flag */
    if (fdc->drive[us].track == 0) {
        st3 |= 0x10;  /* T0 bit */
    }
    /* Ready flag */
    if (fdc->drive[us].ready) {
        st3 |= 0x20;  /* RDY bit */
    }
    fdc->result[0] = st3;
    fdc->result_len = 1;
    _i8272_enter_result(fdc);
}

/*
    _i8272_exec_seek

    SEEK: move head to specified track.
    Generates interrupt. No result phase.
*/
static void _i8272_exec_seek(i8272_t *fdc) {
    uint8_t us = fdc->cmd[1] & 0x03;
    uint8_t ncn = fdc->cmd[2]; /* new cylinder number */
    fdc->drive[us].track = ncn;
    fdc->st0 = I8272_ST0_SE | (us & 0x03);
    _i8272_enter_idle(fdc);
    _i8272_schedule_irq(fdc, 64, true, false);
}

/*
    _i8272_exec_read_data

    READ DATA: read sector(s) from disk.
    Command format: cmd, HU, C, H, R, N, EOT, GPL, DTL
    Result: ST0, ST1, ST2, C, H, R, N
*/
static void _i8272_exec_read_data(i8272_t *fdc) {
    uint8_t us = fdc->cmd[1] & 0x03;
    uint8_t hd = (fdc->cmd[1] >> 2) & 0x01;
    uint8_t c  = fdc->cmd[2];  /* cylinder */
    uint8_t h  = fdc->cmd[3];  /* head */
    uint8_t r  = fdc->cmd[4];  /* sector */
    uint8_t n  = fdc->cmd[5];  /* sector size code (0=128, 1=256, 2=512, 3=1024) */
    uint8_t eot = fdc->cmd[6]; /* end-of-track sector */
    fdc->last_us = us;
    fdc->last_c = c;
    fdc->last_h = h;
    fdc->last_r = r;
    fdc->last_n = n;

    fdc->drive[us].head = hd;
    /*
        Partner boot loaders treat READ DATA success as ST0 == 0.
        Keep SEEK/SENSE-specific unit bits in SENSE paths, but report plain
        normal-termination ST0 for data transfers.
    */
    fdc->st0 = 0;
    fdc->st1 = 0;
    fdc->st2 = 0;

    /* Calculate sector size */
    const uint16_t sector_size = _i8272_sector_size(n);

    bool ok = false;
    if (fdc->read_sector) {
        ok = fdc->read_sector(us, c, h, r, n, fdc->data_buf, fdc->user_data);
    }
    if (ok) {
        /* Success */
        fdc->data_len = sector_size;
        fdc->data_idx = 0;
        fdc->st0 |= I8272_ST0_IC_NT;
        /*
            uPD765/i8272 sets ST1.EN when the transfer reaches EOT.
            GDP Partner ROM uses one-sector READ DATA with EOT=R and expects
            ST1=0x80 as success marker.
        */
        if (r == eot) {
            fdc->st1 |= I8272_ST1_EN;
        }
    } else {
        /* Sector not found */
        fdc->data_len = 0;
        fdc->data_idx = 0;
        fdc->st0 |= I8272_ST0_IC_AT;
        fdc->st1 |= I8272_ST1_ND | I8272_ST1_MA;
    }
    fdc->last_read_ok = ok;

    /* Prepare result phase */
    _i8272_set_rw_result(fdc, c, h, r, n);

    if (ok && fdc->data_len > 0) {
        /* Enter transfer phase immediately: data is sourced by DMA pacing. */
        fdc->phase = I8272_PHASE_EXECUTE;
        fdc->msr = I8272_MSR_RQM | I8272_MSR_DIO | I8272_MSR_EXM | I8272_MSR_CB;
    } else {
        /*
            Command failed before data phase: expose result immediately and
            assert IRQ so firmware can consume ST0/ST1/ST2.
        */
        _i8272_enter_result(fdc);
        /*
            Keep RESULT visible immediately, but delay INTRQ slightly so ROM
            polling code reliably reaches its EI/HALT wait point before the
            completion interrupt is observed.
        */
        _i8272_schedule_irq(fdc, 1024, false, false);
    }
}

/*
    _i8272_exec_write_data

    WRITE DATA: write sector(s) to disk.
    Command format: cmd, HU, C, H, R, N, EOT, GPL, DTL
    Result: ST0, ST1, ST2, C, H, R, N
*/
static void _i8272_exec_write_data(i8272_t *fdc) {
    const uint8_t us = fdc->cmd[1] & 0x03;
    const uint8_t hd = (fdc->cmd[1] >> 2) & 0x01;
    const uint8_t c  = fdc->cmd[2];
    const uint8_t h  = fdc->cmd[3];
    const uint8_t r  = fdc->cmd[4];
    const uint8_t n  = fdc->cmd[5];
    const uint8_t eot = fdc->cmd[6];
    fdc->last_us = us;
    fdc->last_c = c;
    fdc->last_h = h;
    fdc->last_r = r;
    fdc->last_n = n;

    fdc->drive[us].head = hd;
    fdc->st0 = 0;
    fdc->st1 = 0;
    fdc->st2 = 0;
    fdc->data_len = 0;
    fdc->data_idx = 0;

    if (!fdc->drive[us].ready) {
        fdc->st0 = I8272_ST0_IC_ATRDY;
        fdc->st1 = I8272_ST1_NW;
    } else if (!fdc->write_sector) {
        fdc->st0 = I8272_ST0_IC_AT;
        fdc->st1 = I8272_ST1_NW;
    } else {
        fdc->data_len = _i8272_sector_size(n);
        fdc->data_idx = 0;
        fdc->st0 = I8272_ST0_IC_NT;
        if (r == eot) {
            fdc->st1 |= I8272_ST1_EN;
        }
    }

    _i8272_set_rw_result(fdc, c, h, r, n);

    if (fdc->data_len > 0) {
        /* Enter data-receive phase: host/DMA pushes bytes into the FDC. */
        fdc->phase = I8272_PHASE_EXECUTE;
        fdc->msr = I8272_MSR_RQM | I8272_MSR_EXM | I8272_MSR_CB;
    } else {
        _i8272_enter_result(fdc);
        _i8272_schedule_irq(fdc, 1024, false, false);
    }
}

/*
    _i8272_exec_format_track

    FORMAT TRACK: receive SC CHRN tuples and fill each described sector with
    the supplied fill byte.
    Command format: cmd, HU, N, SC, GPL, D
    Execute data: (C, H, R, N) repeated SC times
    Result: ST0, ST1, ST2, C, H, R, N
*/
static void _i8272_exec_format_track(i8272_t *fdc) {
    const uint8_t us = fdc->cmd[1] & 0x03;
    const uint8_t hd = (fdc->cmd[1] >> 2) & 0x01;
    const uint8_t n = fdc->cmd[2];
    const uint8_t sc = fdc->cmd[3];
    const uint16_t exec_bytes = (uint16_t)sc * 4u;
    fdc->last_us = us;
    fdc->last_c = 0;
    fdc->last_h = hd;
    fdc->last_r = 0;
    fdc->last_n = n;

    fdc->drive[us].head = hd;
    fdc->st0 = 0;
    fdc->st1 = 0;
    fdc->st2 = 0;
    fdc->data_len = 0;
    fdc->data_idx = 0;

    if (!fdc->drive[us].ready) {
        fdc->st0 = I8272_ST0_IC_ATRDY;
        fdc->st1 = I8272_ST1_NW;
        _i8272_set_rw_result(fdc, 0, hd, 0, n);
        _i8272_enter_result(fdc);
        _i8272_schedule_irq(fdc, 1024, false, false);
        return;
    }
    if (!fdc->write_sector) {
        fdc->st0 = I8272_ST0_IC_AT;
        fdc->st1 = I8272_ST1_NW;
        _i8272_set_rw_result(fdc, 0, hd, 0, n);
        _i8272_enter_result(fdc);
        _i8272_schedule_irq(fdc, 1024, false, false);
        return;
    }
    if (exec_bytes > I8272_SECTOR_SIZE) {
        fdc->st0 = I8272_ST0_IC_AT;
        fdc->st1 = I8272_ST1_OR;
        _i8272_set_rw_result(fdc, 0, hd, 0, n);
        _i8272_enter_result(fdc);
        _i8272_schedule_irq(fdc, 1024, false, false);
        return;
    }

    fdc->data_len = exec_bytes;
    fdc->data_idx = 0;
    fdc->phase = I8272_PHASE_EXECUTE;
    fdc->msr = I8272_MSR_RQM | I8272_MSR_EXM | I8272_MSR_CB;
}

/*
    _i8272_exec_read_id

    READ ID: returns the next sector ID from the current track.
    Result: ST0, ST1, ST2, C, H, R, N
*/
static void _i8272_exec_read_id(i8272_t *fdc) {
    uint8_t us = fdc->cmd[1] & 0x03;
    uint8_t hd = (fdc->cmd[1] >> 2) & 0x01;

    fdc->st0 = I8272_ST0_IC_NT | (hd << 2) | (us & 0x03);
    fdc->st1 = 0;
    fdc->st2 = 0;

    fdc->result[0] = fdc->st0;
    fdc->result[1] = fdc->st1;
    fdc->result[2] = fdc->st2;
    fdc->result[3] = fdc->drive[us].track;
    fdc->result[4] = hd;
    fdc->result[5] = 1;    /* sector 1 */
    fdc->result[6] = 2;    /* 512 bytes */
    fdc->result_len = 7;

    _i8272_enter_result(fdc);
}

/*
    _i8272_exec_invalid

    Handle invalid/unrecognized commands.
*/
static void _i8272_exec_invalid(i8272_t *fdc) {
    fdc->result[0] = I8272_ST0_IC_IC;
    fdc->result_len = 1;
    _i8272_enter_result(fdc);
}

/*
    _i8272_execute_command

    Dispatch command execution after all command bytes are received.
*/
static void _i8272_execute_command(i8272_t *fdc) {
    switch (fdc->cmd_code) {
        case I8272_CMD_SPECIFY:         _i8272_exec_specify(fdc); break;
        case I8272_CMD_SENSE_DRIVE:     _i8272_exec_sense_drive(fdc); break;
        case I8272_CMD_RECALIBRATE:     _i8272_exec_recalibrate(fdc); break;
        case I8272_CMD_SENSE_INT:       _i8272_exec_sense_int(fdc); break;
        case I8272_CMD_READ_DATA:       _i8272_exec_read_data(fdc); break;
        case I8272_CMD_WRITE_DATA:      _i8272_exec_write_data(fdc); break;
        case I8272_CMD_FORMAT_TRACK:    _i8272_exec_format_track(fdc); break;
        case I8272_CMD_READ_ID:         _i8272_exec_read_id(fdc); break;
        case I8272_CMD_SEEK:            _i8272_exec_seek(fdc); break;
        default:                        _i8272_exec_invalid(fdc); break;
    }
}

/*--- Public API ---*/

void i8272_init(i8272_t *fdc) {
    CHIPS_ASSERT(fdc);
    memset(fdc, 0, sizeof(*fdc));
    fdc->read_sector = NULL;
    fdc->write_sector = NULL;
    fdc->user_data = NULL;
    i8272_reset(fdc);
}

void i8272_reset(i8272_t *fdc) {
    CHIPS_ASSERT(fdc);
    /* Preserve callbacks */
    i8272_read_sector_cb cb = fdc->read_sector;
    i8272_write_sector_cb wcb = fdc->write_sector;
    void *ud = fdc->user_data;

    fdc->phase = I8272_PHASE_IDLE;
    fdc->cmd_idx = 0;
    fdc->cmd_len = 0;
    fdc->cmd_code = 0;
    fdc->result_idx = 0;
    fdc->result_len = 0;
    fdc->st0 = I8272_ST0_SE;
    fdc->st1 = 0;
    fdc->st2 = 0;
    fdc->srt = 0;
    fdc->hut = 0;
    fdc->hlt = 0;
    fdc->ndma = false;
    /*
        The board-level emulator schedules the power-on reset-complete
        interrupt when firmware actually programs the FDC interrupt vector
        during fdc_init. Keeping it pending globally from machine reset makes
        prompt-mode startup paths see a stale interrupt long before the ROM is
        ready to acknowledge it.
    */
    fdc->int_pending = false;
    fdc->irq_request = false;
    fdc->irq_delay = 0;
    fdc->irq_sets_sense = false;
    fdc->irq_enters_result = false;
    fdc->exec_delay = 0;
    fdc->exec_pending = false;
    fdc->data_len = 0;
    fdc->data_idx = 0;
    fdc->last_us = 0;
    fdc->last_c = 0;
    fdc->last_h = 0;
    fdc->last_r = 0;
    fdc->last_n = 0;
    fdc->last_read_ok = false;

    for (int i = 0; i < I8272_MAX_DRIVES; i++) {
        fdc->drive[i].track = 0;
        fdc->drive[i].head = 0;
        fdc->drive[i].motor = false;
        fdc->drive[i].ready = false;
    }

    /* RQM=1, ready for commands */
    fdc->msr = I8272_MSR_RQM;

    /* Restore callbacks */
    fdc->read_sector = cb;
    fdc->write_sector = wcb;
    fdc->user_data = ud;
}

void i8272_tick(i8272_t *fdc) {
    CHIPS_ASSERT(fdc);
    if (fdc->exec_pending) {
        if (fdc->exec_delay > 0) {
            --fdc->exec_delay;
        }
        if (fdc->exec_delay == 0) {
            fdc->exec_pending = false;
            fdc->phase = I8272_PHASE_EXECUTE;
            fdc->msr = I8272_MSR_RQM | I8272_MSR_EXM | I8272_MSR_CB;
            if ((fdc->cmd_code != I8272_CMD_WRITE_DATA) &&
                (fdc->cmd_code != I8272_CMD_FORMAT_TRACK)) {
                fdc->msr |= I8272_MSR_DIO;
            }
        }
    }
    if (fdc->irq_request || (fdc->irq_delay == 0)) {
        return;
    }
    if (--fdc->irq_delay != 0) {
        return;
    }
    if (fdc->irq_sets_sense) {
        fdc->int_pending = true;
    }
    if (fdc->irq_enters_result) {
        _i8272_enter_result(fdc);
    }
    fdc->irq_sets_sense = false;
    fdc->irq_enters_result = false;
    fdc->irq_request = true;
}

uint64_t i8272_tick_pins(i8272_t *fdc, uint64_t pins) {
    CHIPS_ASSERT(fdc);

    if ((pins & I8272_RESET) == 0) {
        i8272_reset(fdc);
        pins &= ~I8272_IRQ;
        return pins;
    }

    /* progress internal delayed execution/IRQ state */
    i8272_tick(fdc);

    if ((pins & I8272_CS) == 0) {
        const uint8_t a = I8272_GET_ADDR(pins) & 0x01;
        if ((pins & I8272_RD) == 0) {
            const uint8_t data = (a == 0) ? i8272_read_status(fdc) : i8272_read_data(fdc);
            I8272_SET_DATA(pins, data);
        } else if ((pins & I8272_WR) == 0) {
            if (a == 1) {
                i8272_write_data(fdc, I8272_GET_DATA(pins));
            }
        }
    }

    if (fdc->irq_request) {
        pins |= I8272_IRQ;
    } else {
        pins &= ~I8272_IRQ;
    }
    return pins;
}

uint8_t i8272_read_status(i8272_t *fdc) {
    CHIPS_ASSERT(fdc);
    return fdc->msr;
}

uint8_t i8272_read_data(i8272_t *fdc) {
    CHIPS_ASSERT(fdc);

    if ((fdc->phase == I8272_PHASE_EXECUTE) &&
        (fdc->cmd_code == I8272_CMD_READ_DATA)) {
        /* Execution phase: return sector data bytes */
        if (fdc->data_idx < fdc->data_len) {
            uint8_t data = fdc->data_buf[fdc->data_idx++];
            if (fdc->data_idx >= fdc->data_len) {
                /*
                    End-of-transfer: raise IRQ and enter RESULT phase so CPU
                    can fetch ST0..N response bytes.
                */
                _i8272_enter_result(fdc);
                /*
                    Schedule (don't instant-fire) completion IRQ to avoid
                    racing ahead of firmware's HALT wait point.
                */
                _i8272_schedule_irq(fdc, 1024, false, false);
            }
            return data;
        }
    }
    else if (fdc->phase == I8272_PHASE_RESULT) {
        /* Result phase: return result bytes */
        if (fdc->result_idx < fdc->result_len) {
            uint8_t data = fdc->result[fdc->result_idx++];
            if (fdc->result_idx >= fdc->result_len) {
                /* All results read, back to idle */
                _i8272_enter_idle(fdc);
            }
            return data;
        }
    }

    return 0xFF;
}

void i8272_write_data(i8272_t *fdc, uint8_t data) {
    CHIPS_ASSERT(fdc);

    if ((fdc->phase == I8272_PHASE_EXECUTE) &&
        ((fdc->cmd_code == I8272_CMD_WRITE_DATA) ||
         (fdc->cmd_code == I8272_CMD_FORMAT_TRACK))) {
        if (fdc->data_idx < fdc->data_len) {
            fdc->data_buf[fdc->data_idx++] = data;
            if (fdc->data_idx >= fdc->data_len) {
                bool ok = false;
                if (fdc->cmd_code == I8272_CMD_WRITE_DATA) {
                    ok = false;
                    if (fdc->write_sector) {
                        ok = fdc->write_sector(
                            fdc->last_us, fdc->last_c, fdc->last_h, fdc->last_r, fdc->last_n,
                            fdc->data_buf, fdc->user_data);
                    }
                    if (!ok) {
                        fdc->st0 = fdc->drive[fdc->last_us].ready ?
                            I8272_ST0_IC_AT : I8272_ST0_IC_ATRDY;
                        fdc->st1 &= (uint8_t)~I8272_ST1_EN;
                        fdc->st1 |= I8272_ST1_NW;
                        _i8272_set_rw_result(
                            fdc, fdc->last_c, fdc->last_h, fdc->last_r, fdc->last_n);
                    }
                } else {
                    ok = _i8272_finalize_format_track(
                        fdc, fdc->last_us, fdc->cmd[3], fdc->cmd[5]);
                }
                _i8272_enter_result(fdc);
                _i8272_schedule_irq(fdc, 1024, false, false);
            }
        }
    }
    else if (fdc->phase == I8272_PHASE_IDLE) {
        /* First command byte: decode command */
        fdc->cmd[0] = data;
        fdc->cmd_code = data & 0x1F; /* lower 5 bits are the command */
        fdc->cmd_len = _i8272_cmd_length(fdc->cmd_code);
        fdc->cmd_idx = 1;

        if (fdc->cmd_len <= 1) {
            /* Single-byte command (e.g. SENSE INTERRUPT STATUS) */
            _i8272_execute_command(fdc);
        } else {
            /* More bytes expected */
            fdc->phase = I8272_PHASE_COMMAND;
            fdc->msr = I8272_MSR_RQM | I8272_MSR_CB; /* RQM=1, CB=1, DIO=0 */
        }
    }
    else if (fdc->phase == I8272_PHASE_COMMAND) {
        /* Subsequent command bytes */
        if (fdc->cmd_idx < 9) {
            fdc->cmd[fdc->cmd_idx++] = data;
        }
        if (fdc->cmd_idx >= fdc->cmd_len) {
            /* All command bytes received, execute */
            _i8272_execute_command(fdc);
        }
    }
}

#endif /* CHIPS_IMPL */
