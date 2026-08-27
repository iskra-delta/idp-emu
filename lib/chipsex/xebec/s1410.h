#pragma once
/*#
    # s1410.h

    Header-only emulator for the Xebec S1410 SASI disk controller.

    The command and bus-phase model follows the S1410 Owner's Manual:

    - selection holds BSY for the complete transaction
    - every device-control block is six bytes
    - command, data, status, and message phases have distinct SASI signals
    - completion consists of one status byte followed by one null message byte
    - class-0 READ/WRITE use a 21-bit logical block address and a zero block
      count means 256 blocks
    - INITIALIZE DRIVE CHARACTERISTICS consumes eight data-out bytes
    - REQUEST SENSE STATUS returns four sense bytes before completion status

    Media access is supplied by 256-byte logical-block callbacks. Mechanical
    seek and rotational delay are intentionally left to the machine wrapper.
#*/
/*
    zlib/libpng license

    Copyright (c) 2026 Tomaz Stih
    This software is provided 'as-is', without any express or implied warranty.
*/
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define S1410_GET_ADDR(p)      ((uint8_t)((p) & 0x03))
#define S1410_GET_DATA(p)      ((uint8_t)(((p) >> 16) & 0xFF))
#define S1410_SET_DATA(p, d)   { p = ((p) & ~0xFF0000ULL) | (((uint64_t)(d) << 16) & 0xFF0000ULL); }

#define S1410_PIN_RD           (24)
#define S1410_PIN_WR           (25)
#define S1410_PIN_CS           (26)
#define S1410_PIN_RESET        (27)

#define S1410_RD               (1ULL << S1410_PIN_RD)
#define S1410_WR               (1ULL << S1410_PIN_WR)
#define S1410_CS               (1ULL << S1410_PIN_CS)
#define S1410_RESET            (1ULL << S1410_PIN_RESET)

/* Legacy register-style status helpers. The Partner adapter derives its
   actual SASI status byte directly from the phase and handshake state. */
#define S1410_STATUS_REQ       (1 << 7)
#define S1410_STATUS_DONE      (1 << 6)
#define S1410_STATUS_RESP      (1 << 4)
#define S1410_STATUS_BUSY      (1 << 3)

#define S1410_MAX_CONFIG       32
#define S1410_MAX_RESPONSE     8
#define S1410_BLOCK_SIZE       256U
/* A six-byte transfer count of zero denotes 256 256-byte blocks. */
#define S1410_MAX_DATA         0x10000U

typedef enum {
    S1410_PHASE_IDLE = 0,
    S1410_PHASE_COMMAND,
    S1410_PHASE_READ_DATA,
    S1410_PHASE_WRITE_DATA,
    S1410_PHASE_STATUS,
    S1410_PHASE_MESSAGE,
    /* Source-compatibility names used by older board diagnostics. */
    S1410_PHASE_AWAIT_CONFIG = S1410_PHASE_COMMAND,
    S1410_PHASE_RESPONSE = S1410_PHASE_STATUS,
} s1410_phase_t;

typedef bool (*s1410_read_blocks_cb)(uint32_t lba, uint32_t count,
                                     uint8_t *dst, void *user);
typedef bool (*s1410_write_blocks_cb)(uint32_t lba, uint32_t count,
                                      const uint8_t *src, void *user);

typedef struct {
    s1410_phase_t phase;
    uint8_t error;

    uint8_t cfg_buf[S1410_MAX_CONFIG];
    uint8_t cfg_len;
    uint8_t cfg_expected;
    uint8_t cfg_kind;

    uint8_t response[S1410_MAX_RESPONSE];
    uint8_t response_len;
    uint8_t response_idx;

    uint8_t sense[4];
    uint8_t sector_buffer[S1410_BLOCK_SIZE];
    uint8_t data[S1410_MAX_DATA];
    uint32_t data_len;
    uint32_t data_idx;
    uint32_t xfer_lba;
    uint32_t xfer_count;

    uint16_t cylinders;
    uint16_t reduced_current_cylinder;
    uint16_t write_precomp_cylinder;
    uint8_t heads;
    uint8_t sectors_per_track;
    uint8_t ecc_burst_length;
    uint8_t lun;

    bool present;
    bool busy;
    bool selected;
    bool initialized;
    bool config_complete;
    bool response_ready;

    s1410_read_blocks_cb read_blocks;
    s1410_write_blocks_cb write_blocks;
    void *user_data;
} s1410_t;

void s1410_init(s1410_t *ctrl);
void s1410_reset(s1410_t *ctrl);
uint8_t s1410_read_status(s1410_t *ctrl);
uint8_t s1410_read_data(s1410_t *ctrl);
uint8_t s1410_read_error(s1410_t *ctrl);
void s1410_write_control(s1410_t *ctrl, uint8_t data);
void s1410_write_data(s1410_t *ctrl, uint8_t data);
uint64_t s1410_tick_pins(s1410_t *ctrl, uint64_t pins);
void s1410_set_present(s1410_t *ctrl, bool present);
bool s1410_is_present(const s1410_t *ctrl);
void s1410_set_block_callbacks(s1410_t *ctrl,
                               s1410_read_blocks_cb read_blocks,
                               s1410_write_blocks_cb write_blocks,
                               void *user_data);

#ifdef __cplusplus
}
#endif

#ifdef CHIPS_IMPL
#ifndef CHIPS_ASSERT
    #include <assert.h>
    #define CHIPS_ASSERT(c) assert(c)
#endif

enum {
    _S1410_DATA_NONE = 0,
    _S1410_DATA_INIT,
    _S1410_DATA_WRITE_BLOCKS,
    _S1410_DATA_WRITE_BUFFER,
    _S1410_DATA_WRITE_LONG,
};

static inline uint32_t _s1410_lba(const uint8_t *dcb) {
    return ((uint32_t)(dcb[1] & 0x1FU) << 16) |
           ((uint32_t)dcb[2] << 8) | (uint32_t)dcb[3];
}

static inline uint32_t _s1410_block_count(uint8_t encoded) {
    return encoded == 0 ? 256U : (uint32_t)encoded;
}

static inline uint32_t _s1410_capacity(const s1410_t *ctrl) {
    return (uint32_t)ctrl->cylinders * (uint32_t)ctrl->heads *
           (uint32_t)ctrl->sectors_per_track;
}

static inline bool _s1410_valid_range(const s1410_t *ctrl,
                                      uint32_t lba, uint32_t count) {
    const uint32_t capacity = _s1410_capacity(ctrl);
    return count != 0 && lba < capacity && count <= (capacity - lba);
}

static inline void _s1410_set_sense(s1410_t *ctrl, uint8_t error,
                                    uint32_t lba, bool address_valid) {
    ctrl->error = error;
    ctrl->sense[0] = (uint8_t)(error | (address_valid ? 0x80U : 0U));
    ctrl->sense[1] = (uint8_t)((ctrl->lun << 5) | ((lba >> 16) & 0x1FU));
    ctrl->sense[2] = (uint8_t)(lba >> 8);
    ctrl->sense[3] = (uint8_t)lba;
}

static inline void _s1410_begin_completion(s1410_t *ctrl, bool error) {
    ctrl->response[0] = (uint8_t)((ctrl->lun << 5) | (error ? 0x02U : 0U));
    ctrl->response[1] = 0;
    ctrl->response_len = 2;
    ctrl->response_idx = 0;
    ctrl->response_ready = true;
    ctrl->config_complete = true;
    ctrl->phase = S1410_PHASE_STATUS;
}

static inline void _s1410_complete_ok(s1410_t *ctrl) {
    ctrl->error = 0;
    _s1410_begin_completion(ctrl, false);
}

static inline void _s1410_complete_error(s1410_t *ctrl, uint8_t error,
                                         uint32_t lba, bool address_valid) {
    _s1410_set_sense(ctrl, error, lba, address_valid);
    _s1410_begin_completion(ctrl, true);
}

static inline void _s1410_begin_read(s1410_t *ctrl, uint32_t len) {
    ctrl->data_len = len;
    ctrl->data_idx = 0;
    ctrl->phase = S1410_PHASE_READ_DATA;
}

static inline void _s1410_begin_write(s1410_t *ctrl, uint8_t kind,
                                      uint32_t len) {
    ctrl->cfg_kind = kind;
    ctrl->data_len = len;
    ctrl->data_idx = 0;
    ctrl->phase = S1410_PHASE_WRITE_DATA;
}

static inline void _s1410_execute_command(s1410_t *ctrl) {
    const uint8_t opcode = ctrl->cfg_buf[0];
    const uint32_t lba = _s1410_lba(ctrl->cfg_buf);
    const uint32_t count = _s1410_block_count(ctrl->cfg_buf[4]);
    ctrl->lun = (uint8_t)((ctrl->cfg_buf[1] >> 5) & 0x01U);
    ctrl->xfer_lba = lba;
    ctrl->xfer_count = count;
    ctrl->cfg_kind = _S1410_DATA_NONE;

    switch (opcode) {
        case 0x00: /* TEST DRIVE READY */
        case 0x01: /* RECALIBRATE */
            _s1410_complete_ok(ctrl);
            break;

        case 0x03: /* REQUEST SENSE STATUS */
            memcpy(ctrl->data, ctrl->sense, sizeof(ctrl->sense));
            _s1410_begin_read(ctrl, sizeof(ctrl->sense));
            break;

        case 0x04: /* FORMAT DRIVE */
        case 0x05: /* CHECK TRACK FORMAT */
        case 0x06: /* FORMAT TRACK */
        case 0x07: /* FORMAT BAD TRACK */
        case 0x0E: /* FORMAT ALTERNATE TRACK, revision E */
            /* The logical image has no header/bad-track metadata. Validate the
               address and report the controller-visible completion contract. */
            if (_s1410_valid_range(ctrl, lba, 1))
                _s1410_complete_ok(ctrl);
            else
                _s1410_complete_error(ctrl, 0x21, lba, true);
            break;

        case 0x08: { /* READ */
            const uint32_t bytes = count * S1410_BLOCK_SIZE;
            if (bytes > S1410_MAX_DATA || !_s1410_valid_range(ctrl, lba, count)) {
                _s1410_complete_error(ctrl, 0x21, lba, true);
            } else if (ctrl->read_blocks &&
                       ctrl->read_blocks(lba, count, ctrl->data, ctrl->user_data)) {
                _s1410_begin_read(ctrl, bytes);
            } else {
                _s1410_complete_error(ctrl, 0x14, lba, true);
            }
            break;
        }

        case 0x0A: { /* WRITE */
            const uint32_t bytes = count * S1410_BLOCK_SIZE;
            if (bytes > S1410_MAX_DATA || !_s1410_valid_range(ctrl, lba, count))
                _s1410_complete_error(ctrl, 0x21, lba, true);
            else if (!ctrl->write_blocks)
                _s1410_complete_error(ctrl, 0x20, lba, true);
            else
                _s1410_begin_write(ctrl, _S1410_DATA_WRITE_BLOCKS, bytes);
            break;
        }

        case 0x0B: /* SEEK */
            if (_s1410_valid_range(ctrl, lba, 1))
                _s1410_complete_ok(ctrl);
            else
                _s1410_complete_error(ctrl, 0x21, lba, true);
            break;

        case 0x0C: /* INITIALIZE DRIVE CHARACTERISTICS */
            _s1410_begin_write(ctrl, _S1410_DATA_INIT, 8);
            break;

        case 0x0F: /* WRITE SECTOR BUFFER, revision E */
            _s1410_begin_write(ctrl, _S1410_DATA_WRITE_BUFFER,
                               S1410_BLOCK_SIZE);
            break;

        case 0x10: /* READ SECTOR BUFFER, revision E */
            memcpy(ctrl->data, ctrl->sector_buffer, S1410_BLOCK_SIZE);
            _s1410_begin_read(ctrl, S1410_BLOCK_SIZE);
            break;

        case 0xE0: /* RAM DIAGNOSTIC */
        case 0xE3: /* DRIVE DIAGNOSTIC */
        case 0xE4: /* CONTROLLER INTERNAL DIAGNOSTIC */
            _s1410_complete_ok(ctrl);
            break;

        case 0xE5: /* READ LONG: sector plus four ECC bytes */
            if (!_s1410_valid_range(ctrl, lba, 1)) {
                _s1410_complete_error(ctrl, 0x21, lba, true);
            } else if (ctrl->read_blocks &&
                       ctrl->read_blocks(lba, 1, ctrl->data, ctrl->user_data)) {
                memset(ctrl->data + S1410_BLOCK_SIZE, 0, 4);
                _s1410_begin_read(ctrl, S1410_BLOCK_SIZE + 4U);
            } else {
                _s1410_complete_error(ctrl, 0x14, lba, true);
            }
            break;

        case 0xE6: /* WRITE LONG: sector plus four ECC bytes */
            if (!_s1410_valid_range(ctrl, lba, 1) || !ctrl->write_blocks)
                _s1410_complete_error(ctrl, 0x21, lba, true);
            else
                _s1410_begin_write(ctrl, _S1410_DATA_WRITE_LONG,
                                   S1410_BLOCK_SIZE + 4U);
            break;

        default:
            /* Type 2/code 0: invalid command. Unknown commands must never be
               reported as successful because firmware uses this for probing. */
            _s1410_complete_error(ctrl, 0x20, lba, false);
            break;
    }
}

static inline void _s1410_complete_data_out(s1410_t *ctrl) {
    switch (ctrl->cfg_kind) {
        case _S1410_DATA_INIT:
            ctrl->cylinders = (uint16_t)(((uint16_t)ctrl->data[0] << 8) |
                                         ctrl->data[1]);
            ctrl->heads = ctrl->data[2];
            ctrl->reduced_current_cylinder =
                (uint16_t)(((uint16_t)ctrl->data[3] << 8) | ctrl->data[4]);
            ctrl->write_precomp_cylinder =
                (uint16_t)(((uint16_t)ctrl->data[5] << 8) | ctrl->data[6]);
            ctrl->ecc_burst_length = ctrl->data[7];
            if (ctrl->cylinders == 0 || ctrl->heads == 0)
                _s1410_complete_error(ctrl, 0x22, 0, false);
            else {
                ctrl->initialized = true;
                _s1410_complete_ok(ctrl);
            }
            break;

        case _S1410_DATA_WRITE_BLOCKS:
            if (ctrl->write_blocks && ctrl->write_blocks(
                    ctrl->xfer_lba, ctrl->xfer_count,
                    ctrl->data, ctrl->user_data))
                _s1410_complete_ok(ctrl);
            else
                _s1410_complete_error(ctrl, 0x14, ctrl->xfer_lba, true);
            break;

        case _S1410_DATA_WRITE_BUFFER:
            memcpy(ctrl->sector_buffer, ctrl->data, S1410_BLOCK_SIZE);
            _s1410_complete_ok(ctrl);
            break;

        case _S1410_DATA_WRITE_LONG:
            if (ctrl->write_blocks && ctrl->write_blocks(
                    ctrl->xfer_lba, 1, ctrl->data, ctrl->user_data))
                _s1410_complete_ok(ctrl);
            else
                _s1410_complete_error(ctrl, 0x14, ctrl->xfer_lba, true);
            break;

        default:
            _s1410_complete_error(ctrl, 0x20, 0, false);
            break;
    }
}

void s1410_init(s1410_t *ctrl) {
    CHIPS_ASSERT(ctrl);
    memset(ctrl, 0, sizeof(*ctrl));
    s1410_reset(ctrl);
}

void s1410_set_present(s1410_t *ctrl, bool present) {
    CHIPS_ASSERT(ctrl);
    ctrl->present = present;
}

bool s1410_is_present(const s1410_t *ctrl) {
    CHIPS_ASSERT(ctrl);
    return ctrl->present;
}

void s1410_set_block_callbacks(s1410_t *ctrl,
                               s1410_read_blocks_cb read_blocks,
                               s1410_write_blocks_cb write_blocks,
                               void *user_data) {
    CHIPS_ASSERT(ctrl);
    ctrl->read_blocks = read_blocks;
    ctrl->write_blocks = write_blocks;
    ctrl->user_data = user_data;
}

void s1410_reset(s1410_t *ctrl) {
    CHIPS_ASSERT(ctrl);
    const s1410_read_blocks_cb read_cb = ctrl->read_blocks;
    const s1410_write_blocks_cb write_cb = ctrl->write_blocks;
    void *const user_data = ctrl->user_data;
    const bool present = ctrl->present;
    memset(ctrl, 0, sizeof(*ctrl));
    ctrl->read_blocks = read_cb;
    ctrl->write_blocks = write_cb;
    ctrl->user_data = user_data;
    ctrl->present = present;
    ctrl->phase = S1410_PHASE_IDLE;
    ctrl->cylinders = 153;
    ctrl->heads = 4;
    ctrl->sectors_per_track = 32;
    ctrl->reduced_current_cylinder = 128;
    ctrl->write_precomp_cylinder = 64;
    ctrl->ecc_burst_length = 11;
}

uint8_t s1410_read_status(s1410_t *ctrl) {
    CHIPS_ASSERT(ctrl);
    if (!ctrl->present)
        return 0;
    uint8_t status = 0;
    if (ctrl->phase != S1410_PHASE_IDLE || ctrl->selected)
        status |= S1410_STATUS_REQ;
    if (ctrl->busy)
        status |= S1410_STATUS_BUSY;
    if (ctrl->config_complete)
        status |= S1410_STATUS_DONE;
    if (ctrl->response_ready)
        status |= S1410_STATUS_RESP;
    return status;
}

uint8_t s1410_read_data(s1410_t *ctrl) {
    CHIPS_ASSERT(ctrl);
    if (!ctrl->present)
        return 0xFF;

    if (ctrl->phase == S1410_PHASE_READ_DATA) {
        if (ctrl->data_idx >= ctrl->data_len)
            return 0xFF;
        const uint8_t value = ctrl->data[ctrl->data_idx++];
        if (ctrl->data_idx == ctrl->data_len)
            _s1410_complete_ok(ctrl);
        return value;
    }

    if (ctrl->phase == S1410_PHASE_STATUS) {
        ctrl->response_idx = 1;
        ctrl->phase = S1410_PHASE_MESSAGE;
        return ctrl->response[0];
    }

    if (ctrl->phase == S1410_PHASE_MESSAGE) {
        ctrl->response_idx = 2;
        ctrl->response_ready = false;
        ctrl->busy = false;
        ctrl->selected = false;
        ctrl->phase = S1410_PHASE_IDLE;
        return ctrl->response[1];
    }

    return 0xFF;
}

uint8_t s1410_read_error(s1410_t *ctrl) {
    CHIPS_ASSERT(ctrl);
    return ctrl->error;
}

void s1410_write_control(s1410_t *ctrl, uint8_t data) {
    CHIPS_ASSERT(ctrl);
    if (!ctrl->present)
        return;

    switch (data) {
        case 0x00: /* release bus / abort session */
            ctrl->busy = false;
            ctrl->selected = false;
            ctrl->config_complete = false;
            ctrl->response_ready = false;
            ctrl->response_len = 0;
            ctrl->response_idx = 0;
            ctrl->phase = S1410_PHASE_IDLE;
            break;

        case 0x01: /* SEL asserted */
            ctrl->busy = true;
            ctrl->selected = true;
            ctrl->config_complete = false;
            ctrl->response_ready = false;
            ctrl->cfg_len = 0;
            ctrl->cfg_expected = 0;
            ctrl->cfg_kind = _S1410_DATA_NONE;
            ctrl->data_len = 0;
            ctrl->data_idx = 0;
            ctrl->phase = S1410_PHASE_IDLE;
            break;

        case 0x02: /* SEL released; target requests the six-byte DCB */
            if (ctrl->busy) {
                ctrl->selected = false;
                ctrl->cfg_len = 0;
                ctrl->cfg_expected = 6;
                ctrl->phase = S1410_PHASE_COMMAND;
            }
            break;

        default:
            break;
    }
}

void s1410_write_data(s1410_t *ctrl, uint8_t data) {
    CHIPS_ASSERT(ctrl);
    if (!ctrl->present)
        return;

    if (ctrl->phase == S1410_PHASE_COMMAND) {
        if (ctrl->cfg_len < 6)
            ctrl->cfg_buf[ctrl->cfg_len++] = data;
        if (ctrl->cfg_len == 6)
            _s1410_execute_command(ctrl);
        return;
    }

    if (ctrl->phase == S1410_PHASE_WRITE_DATA) {
        if (ctrl->data_idx < ctrl->data_len)
            ctrl->data[ctrl->data_idx++] = data;
        if (ctrl->data_idx == ctrl->data_len)
            _s1410_complete_data_out(ctrl);
    }
}

uint64_t s1410_tick_pins(s1410_t *ctrl, uint64_t pins) {
    CHIPS_ASSERT(ctrl);
    if ((pins & S1410_RESET) == 0) {
        s1410_reset(ctrl);
        return pins;
    }
    if ((pins & S1410_CS) == 0) {
        const uint8_t address = S1410_GET_ADDR(pins);
        if ((pins & S1410_RD) == 0) {
            uint8_t data = 0xFF;
            if (address == 0)
                data = s1410_read_status(ctrl);
            else if (address == 1)
                data = s1410_read_data(ctrl);
            else if (address == 2)
                data = s1410_read_error(ctrl);
            S1410_SET_DATA(pins, data);
        } else if ((pins & S1410_WR) == 0) {
            const uint8_t data = S1410_GET_DATA(pins);
            if (address == 0 || address == 2)
                s1410_write_control(ctrl, data);
            else if (address == 1)
                s1410_write_data(ctrl, data);
        }
    }
    return pins;
}

#endif
