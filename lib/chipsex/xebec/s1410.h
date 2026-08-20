#pragma once
/*#
    # s1410.h

    Header-only minimal emulator for the Xebec S1410 SASI hard disk controller.

    This models the small command/status/data subset currently needed by the
    Iskra Delta Partner ROM:

    - Status/control at port 0x10
    - Data at port 0x11
    - Error/reset at port 0x12
    - Simple configuration exchanges
    - READ/WRITE(6), READ/WRITE(10), and IDENTIFY-style command handling
      from SASI command bytes

    The goal is to provide a reusable chip-style device that can be wired into
    a machine wrapper, rather than keeping HDD logic embedded in board code.

    ## Emulated Pins (CHIPS-style)

    ***************************************
    *           +-----------+             *
    * D0..D7 <->|           |<-> A0..A1   *
    *           |   XEbec   |             *
    *   CS̅  --->|   S1410   |             *
    *   RD̅  --->|           |             *
    *   WR̅  --->|           |             *
    * RESET̅ --->|           |             *
    *           +-----------+             *
    ***************************************

    - D0..D7: Bidirectional data bus (status/data/error and command bytes)
    - A0..A1: Register select
      - 0: status/control (port 0x10 semantics)
      - 1: data (port 0x11 semantics)
      - 2: error/control (port 0x12 semantics)
    - CS̅, RD̅, WR̅, RESET̅: active-low control pins
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

/* bus helpers: address in bits 0..1, data in bits 16..23 */
#define S1410_GET_ADDR(p)      ((uint8_t)((p) & 0x03))
#define S1410_GET_DATA(p)      ((uint8_t)(((p) >> 16) & 0xFF))
#define S1410_SET_DATA(p, d)   { p = ((p) & ~0xFF0000ULL) | (((uint64_t)(d) << 16) & 0xFF0000ULL); }

/* active-low bus pins */
#define S1410_PIN_RD           (24)
#define S1410_PIN_WR           (25)
#define S1410_PIN_CS           (26)
#define S1410_PIN_RESET        (27)

#define S1410_RD               (1ULL << S1410_PIN_RD)
#define S1410_WR               (1ULL << S1410_PIN_WR)
#define S1410_CS               (1ULL << S1410_PIN_CS)
#define S1410_RESET            (1ULL << S1410_PIN_RESET)

#define S1410_STATUS_REQ   (1 << 7)
#define S1410_STATUS_DONE  (1 << 6)
#define S1410_STATUS_RESP  (1 << 4)
#define S1410_STATUS_BUSY  (1 << 3)

#define S1410_MAX_CONFIG   32
#define S1410_MAX_RESPONSE 8
/* READ(6) transfer-length 0 means 256 blocks -> 65536 bytes. */
#define S1410_MAX_DATA     0x10000

typedef enum {
    S1410_PHASE_IDLE = 0,
    S1410_PHASE_AWAIT_CONFIG,
    S1410_PHASE_READ_DATA,
    S1410_PHASE_WRITE_DATA,
    S1410_PHASE_RESPONSE,
} s1410_phase_t;

typedef bool (*s1410_read_blocks_cb)(uint32_t lba, uint32_t count, uint8_t *dst, void *user);
typedef bool (*s1410_write_blocks_cb)(uint32_t lba, uint32_t count, const uint8_t *src, void *user);

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

    uint8_t data[S1410_MAX_DATA];
    uint32_t data_len;
    uint32_t data_idx;
    uint32_t xfer_lba;
    uint32_t xfer_count;

    bool present;
    bool busy;
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

static inline void _s1410_set_response(s1410_t *ctrl, const uint8_t *src, uint8_t len) {
    ctrl->response_len = len;
    ctrl->response_idx = 0;
    ctrl->response_ready = (len > 0);
    if (len > 0) {
        memcpy(ctrl->response, src, len);
        ctrl->phase = S1410_PHASE_RESPONSE;
    } else {
        ctrl->phase = S1410_PHASE_IDLE;
    }
}

static inline bool _s1410_blocks_to_bytes(uint32_t count, uint32_t *out_bytes) {
    const uint32_t max_blocks = S1410_MAX_DATA / 256U;
    if ((count == 0) || (count > max_blocks)) {
        return false;
    }
    *out_bytes = count * 256U;
    return true;
}

static inline void _s1410_fail_command(s1410_t *ctrl, uint8_t error) {
    static const uint8_t err_resp[2] = {0x01, 0x00};
    ctrl->error = error;
    ctrl->config_complete = true;
    ctrl->response_ready = false;
    _s1410_set_response(ctrl, err_resp, 2);
}

void s1410_init(s1410_t *ctrl) {
    CHIPS_ASSERT(ctrl);
    memset(ctrl, 0, sizeof(*ctrl));
    ctrl->read_blocks = NULL;
    ctrl->write_blocks = NULL;
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
    s1410_read_blocks_cb cb = ctrl->read_blocks;
    s1410_write_blocks_cb wcb = ctrl->write_blocks;
    void *ud = ctrl->user_data;
    bool present = ctrl->present;
    memset(ctrl, 0, sizeof(*ctrl));
    ctrl->read_blocks = cb;
    ctrl->write_blocks = wcb;
    ctrl->user_data = ud;
    ctrl->present = present;
    ctrl->phase = S1410_PHASE_IDLE;
}

uint8_t s1410_read_status(s1410_t *ctrl) {
    CHIPS_ASSERT(ctrl);
    if (!ctrl->present) {
        return 0;
    }
    uint8_t status = S1410_STATUS_REQ;
    if (ctrl->busy) status |= S1410_STATUS_BUSY;
    if (ctrl->config_complete) status |= S1410_STATUS_DONE;
    if (ctrl->response_ready) status |= S1410_STATUS_RESP;
    return status;
}

uint8_t s1410_read_data(s1410_t *ctrl) {
    CHIPS_ASSERT(ctrl);
    if (!ctrl->present) {
        return 0xFF;
    }

    if (ctrl->phase == S1410_PHASE_READ_DATA) {
        if (ctrl->data_idx < ctrl->data_len) {
            uint8_t v = ctrl->data[ctrl->data_idx++];
            if (ctrl->data_idx >= ctrl->data_len) {
                static const uint8_t ok_resp[2] = {0x00, 0x00};
                ctrl->config_complete = true;
                _s1410_set_response(ctrl, ok_resp, 2);
            }
            return v;
        }
        return 0xFF;
    }

    if ((ctrl->phase == S1410_PHASE_RESPONSE) && (ctrl->response_idx < ctrl->response_len)) {
        uint8_t v = ctrl->response[ctrl->response_idx++];
        if (ctrl->response_idx >= ctrl->response_len) {
            ctrl->response_ready = false;
            ctrl->phase = S1410_PHASE_IDLE;
        }
        return v;
    }

    return 0xFF;
}

uint8_t s1410_read_error(s1410_t *ctrl) {
    CHIPS_ASSERT(ctrl);
    return ctrl->error;
}

void s1410_write_control(s1410_t *ctrl, uint8_t data) {
    CHIPS_ASSERT(ctrl);
    if (!ctrl->present) {
        return;
    }

    switch (data) {
        case 0x00:
            ctrl->busy = false;
            ctrl->config_complete = false;
            ctrl->response_ready = false;
            ctrl->response_len = 0;
            ctrl->response_idx = 0;
            ctrl->phase = S1410_PHASE_IDLE;
            break;
        case 0x01:
            // The Partner ROM expects bit 3 to assert after the reset pulse.
            ctrl->busy = true;
            ctrl->config_complete = false;
            ctrl->response_ready = false;
            ctrl->cfg_len = 0;
            ctrl->cfg_expected = 0;
            ctrl->cfg_kind = 0;
            ctrl->phase = S1410_PHASE_IDLE;
            break;
        case 0x02:
            // Enter parameter phase: busy drops, port 0x11 becomes writable.
            ctrl->busy = false;
            ctrl->config_complete = false;
            ctrl->response_ready = true;
            ctrl->cfg_len = 0;
            ctrl->cfg_expected = 0;
            ctrl->cfg_kind = 0;
            ctrl->phase = S1410_PHASE_AWAIT_CONFIG;
            break;
        default:
            break;
    }
}

void s1410_write_data(s1410_t *ctrl, uint8_t data) {
    CHIPS_ASSERT(ctrl);
    if (!ctrl->present) {
        return;
    }

    if (ctrl->phase == S1410_PHASE_AWAIT_CONFIG) {
        if (ctrl->cfg_len < S1410_MAX_CONFIG) {
            ctrl->cfg_buf[ctrl->cfg_len++] = data;
        }

        if (ctrl->cfg_len == 1) {
            switch (data) {
                // 6-byte SASI command packets used by Partner ROM:
                // 0x0C REQUEST SENSE / IDENTIFY, 0x08 READ(0).
                case 0x0C: ctrl->cfg_kind = 1; ctrl->cfg_expected = 6; break;
                case 0x08: ctrl->cfg_kind = 2; ctrl->cfg_expected = 6; break;
                case 0x0A: ctrl->cfg_kind = 6; ctrl->cfg_expected = 6; break;
                // 10-byte READ command used by some BIOS paths.
                case 0x28: ctrl->cfg_kind = 5; ctrl->cfg_expected = 10; break;
                case 0x2A: ctrl->cfg_kind = 7; ctrl->cfg_expected = 10; break;
                case 0x01: ctrl->cfg_kind = 4; ctrl->cfg_expected = 1; break;
                // Unknown opcode: keep collecting a full 6-byte packet so REQ/CD
                // handshakes stay coherent instead of dropping immediately to idle.
                default:   ctrl->cfg_kind = 3; ctrl->cfg_expected = 6; break;
            }
        }

        if ((ctrl->cfg_expected > 0) && (ctrl->cfg_len >= ctrl->cfg_expected)) {
            ctrl->config_complete = true;
            ctrl->response_ready = false;
            if (ctrl->cfg_kind == 1) {
                // REQUEST SENSE / identify-style exchange returns 2 status bytes.
                static const uint8_t ok_resp[2] = {0x00, 0x00};
                _s1410_set_response(ctrl, ok_resp, 2);
                ctrl->response_ready = true;
            } else if (ctrl->cfg_kind == 2) {
                // READ(6): 21-bit LBA encoded across CDB bytes 1..3.
                //  byte1: LUN(7:5) + LBA(20:16)
                //  byte2: LBA(15:8)
                //  byte3: LBA(7:0)
                const uint32_t lba = ((uint32_t)(ctrl->cfg_buf[1] & 0x1F) << 16) |
                                     ((uint32_t)ctrl->cfg_buf[2] << 8) |
                                     (uint32_t)ctrl->cfg_buf[3];
                uint32_t count = ctrl->cfg_buf[4];
                // READ(6) transfer-length 0 encodes 256 blocks.
                if (count == 0) {
                    count = 256;
                }
                ctrl->data_idx = 0;
                ctrl->data_len = 0;
                uint32_t bytes = 0;
                if (!_s1410_blocks_to_bytes(count, &bytes)) {
                    _s1410_fail_command(ctrl, 0x32);
                } else if (ctrl->read_blocks &&
                           ctrl->read_blocks(lba, count, ctrl->data, ctrl->user_data)) {
                    ctrl->xfer_lba = lba;
                    ctrl->xfer_count = count;
                    ctrl->data_len = bytes;
                    ctrl->error = 0;
                    ctrl->phase = S1410_PHASE_READ_DATA;
                } else {
                    _s1410_fail_command(ctrl, 0x32);
                }
            } else if (ctrl->cfg_kind == 5) {
                // READ(10): LBA in bytes 2..5, transfer length in bytes 7..8.
                const uint32_t lba = ((uint32_t)ctrl->cfg_buf[2] << 24) |
                                     ((uint32_t)ctrl->cfg_buf[3] << 16) |
                                     ((uint32_t)ctrl->cfg_buf[4] << 8) |
                                     (uint32_t)ctrl->cfg_buf[5];
                uint32_t count = ((uint32_t)ctrl->cfg_buf[7] << 8) |
                                 (uint32_t)ctrl->cfg_buf[8];
                if (count == 0) {
                    count = 65536u;
                }
                ctrl->data_idx = 0;
                ctrl->data_len = 0;
                uint32_t bytes = 0;
                if (!_s1410_blocks_to_bytes(count, &bytes)) {
                    _s1410_fail_command(ctrl, 0x32);
                } else if (ctrl->read_blocks &&
                           ctrl->read_blocks(lba, count, ctrl->data, ctrl->user_data)) {
                    ctrl->xfer_lba = lba;
                    ctrl->xfer_count = count;
                    ctrl->data_len = bytes;
                    ctrl->error = 0;
                    ctrl->phase = S1410_PHASE_READ_DATA;
                } else {
                    _s1410_fail_command(ctrl, 0x32);
                }
            } else if (ctrl->cfg_kind == 6) {
                // WRITE(6): same addressing format as READ(6).
                const uint32_t lba = ((uint32_t)(ctrl->cfg_buf[1] & 0x1F) << 16) |
                                     ((uint32_t)ctrl->cfg_buf[2] << 8) |
                                     (uint32_t)ctrl->cfg_buf[3];
                uint32_t count = ctrl->cfg_buf[4];
                if (count == 0) {
                    count = 256;
                }
                uint32_t bytes = 0;
                if (!_s1410_blocks_to_bytes(count, &bytes) || !ctrl->write_blocks) {
                    _s1410_fail_command(ctrl, 0x32);
                } else {
                    ctrl->xfer_lba = lba;
                    ctrl->xfer_count = count;
                    ctrl->data_idx = 0;
                    ctrl->data_len = bytes;
                    ctrl->error = 0;
                    ctrl->phase = S1410_PHASE_WRITE_DATA;
                }
            } else if (ctrl->cfg_kind == 7) {
                // WRITE(10): same addressing format as READ(10).
                const uint32_t lba = ((uint32_t)ctrl->cfg_buf[2] << 24) |
                                     ((uint32_t)ctrl->cfg_buf[3] << 16) |
                                     ((uint32_t)ctrl->cfg_buf[4] << 8) |
                                     (uint32_t)ctrl->cfg_buf[5];
                uint32_t count = ((uint32_t)ctrl->cfg_buf[7] << 8) |
                                 (uint32_t)ctrl->cfg_buf[8];
                if (count == 0) {
                    count = 65536u;
                }
                uint32_t bytes = 0;
                if (!_s1410_blocks_to_bytes(count, &bytes) || !ctrl->write_blocks) {
                    _s1410_fail_command(ctrl, 0x32);
                } else {
                    ctrl->xfer_lba = lba;
                    ctrl->xfer_count = count;
                    ctrl->data_idx = 0;
                    ctrl->data_len = bytes;
                    ctrl->error = 0;
                    ctrl->phase = S1410_PHASE_WRITE_DATA;
                }
            } else if (ctrl->cfg_kind == 4) {
                // CRT boot uses a 1-byte "drive ready" exchange and still expects
                // a 2-byte completion response via port 0x11 afterwards.
                static const uint8_t ok_resp[2] = {0x00, 0x00};
                _s1410_set_response(ctrl, ok_resp, 2);
                ctrl->response_ready = true;
            } else {
                // Unknown 6-byte command: return a clean completion response so
                // firmware state machines can continue probing/booting.
                static const uint8_t ok_resp[2] = {0x00, 0x00};
                _s1410_set_response(ctrl, ok_resp, 2);
                ctrl->response_ready = true;
            }
        }
        return;
    }

    if (ctrl->phase == S1410_PHASE_WRITE_DATA) {
        if (ctrl->data_idx < ctrl->data_len) {
            ctrl->data[ctrl->data_idx++] = data;
        }
        if (ctrl->data_idx >= ctrl->data_len) {
            static const uint8_t ok_resp[2] = {0x00, 0x00};
            if (ctrl->write_blocks &&
                ctrl->write_blocks(ctrl->xfer_lba, ctrl->xfer_count, ctrl->data, ctrl->user_data)) {
                ctrl->error = 0;
                ctrl->config_complete = true;
                _s1410_set_response(ctrl, ok_resp, 2);
                ctrl->response_ready = true;
            } else {
                _s1410_fail_command(ctrl, 0x32);
            }
        }
    }
}

uint64_t s1410_tick_pins(s1410_t *ctrl, uint64_t pins) {
    CHIPS_ASSERT(ctrl);

    if ((pins & S1410_RESET) == 0) {
        s1410_reset(ctrl);
        return pins;
    }

    if ((pins & S1410_CS) == 0) {
        const uint8_t a = S1410_GET_ADDR(pins);
        if ((pins & S1410_RD) == 0) {
            uint8_t data = 0xFF;
            switch (a) {
                case 0: data = s1410_read_status(ctrl); break;
                case 1: data = s1410_read_data(ctrl); break;
                case 2: data = s1410_read_error(ctrl); break;
                default: break;
            }
            S1410_SET_DATA(pins, data);
        } else if ((pins & S1410_WR) == 0) {
            const uint8_t data = S1410_GET_DATA(pins);
            switch (a) {
                case 0: s1410_write_control(ctrl, data); break;
                case 1: s1410_write_data(ctrl, data); break;
                case 2: s1410_write_control(ctrl, data); break;
                default: break;
            }
        }
    }
    return pins;
}

#endif
