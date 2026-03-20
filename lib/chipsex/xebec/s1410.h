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
    - Read command 0x22 with DMA-driven data fetch

    The goal is to provide a reusable chip-style device that can be wired into
    a machine wrapper, rather than keeping HDD logic embedded in board code.
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

#define S1410_STATUS_REQ   (1 << 7)
#define S1410_STATUS_DONE  (1 << 6)
#define S1410_STATUS_RESP  (1 << 4)
#define S1410_STATUS_BUSY  (1 << 3)

#define S1410_MAX_CONFIG   32
#define S1410_MAX_RESPONSE 8
#define S1410_MAX_DATA     0x2000

typedef enum {
    S1410_PHASE_IDLE = 0,
    S1410_PHASE_AWAIT_CONFIG,
    S1410_PHASE_READ_DATA,
    S1410_PHASE_RESPONSE,
} s1410_phase_t;

typedef bool (*s1410_read_blocks_cb)(uint32_t lba, uint32_t count, uint8_t *dst, void *user);

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

    bool present;
    bool busy;
    bool config_complete;
    bool response_ready;

    s1410_read_blocks_cb read_blocks;
    void *user_data;
} s1410_t;

void s1410_init(s1410_t *ctrl);
void s1410_reset(s1410_t *ctrl);
uint8_t s1410_read_status(s1410_t *ctrl);
uint8_t s1410_read_data(s1410_t *ctrl);
uint8_t s1410_read_error(s1410_t *ctrl);
void s1410_write_control(s1410_t *ctrl, uint8_t data);
void s1410_write_data(s1410_t *ctrl, uint8_t data);

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

void s1410_init(s1410_t *ctrl) {
    CHIPS_ASSERT(ctrl);
    memset(ctrl, 0, sizeof(*ctrl));
    s1410_reset(ctrl);
}

void s1410_reset(s1410_t *ctrl) {
    CHIPS_ASSERT(ctrl);
    s1410_read_blocks_cb cb = ctrl->read_blocks;
    void *ud = ctrl->user_data;
    bool present = ctrl->present;
    memset(ctrl, 0, sizeof(*ctrl));
    ctrl->read_blocks = cb;
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
        case 0x22: {
            ctrl->phase = S1410_PHASE_READ_DATA;
            ctrl->data_idx = 0;
            ctrl->data_len = 0;
            ctrl->config_complete = false;
            ctrl->response_ready = false;
            uint32_t lba = 0;
            uint32_t count = 0x1F;
            if (ctrl->cfg_len >= 6) {
                lba = (uint32_t)ctrl->cfg_buf[1] | ((uint32_t)ctrl->cfg_buf[2] << 8);
                count = ctrl->cfg_buf[4];
                if (count == 0) {
                    count = 1;
                }
            }
            uint32_t bytes = count * 256U;
            if (bytes > S1410_MAX_DATA) {
                bytes = S1410_MAX_DATA;
            }
            if (ctrl->read_blocks && ctrl->read_blocks(lba, count, ctrl->data, ctrl->user_data)) {
                ctrl->data_len = bytes;
                ctrl->error = 0;
            } else {
                static const uint8_t err_resp[2] = {0x01, 0x00};
                ctrl->error = 0x32;
                ctrl->data_len = 0;
                ctrl->config_complete = true;
                _s1410_set_response(ctrl, err_resp, 2);
            }
            break;
        }
        default:
            break;
    }
}

void s1410_write_data(s1410_t *ctrl, uint8_t data) {
    CHIPS_ASSERT(ctrl);
    if (!ctrl->present || (ctrl->phase != S1410_PHASE_AWAIT_CONFIG)) {
        return;
    }

    if (ctrl->cfg_len < S1410_MAX_CONFIG) {
        ctrl->cfg_buf[ctrl->cfg_len++] = data;
    }

    if (ctrl->cfg_len == 1) {
        switch (data) {
            case 0x0C: ctrl->cfg_kind = 1; ctrl->cfg_expected = 20; break;
            case 0x08: ctrl->cfg_kind = 2; ctrl->cfg_expected = 6; break;
            case 0x01: ctrl->cfg_kind = 4; ctrl->cfg_expected = 1; break;
            default:   ctrl->cfg_kind = 3; ctrl->cfg_expected = 1; break;
        }
    }

    if ((ctrl->cfg_expected > 0) && (ctrl->cfg_len >= ctrl->cfg_expected)) {
        ctrl->config_complete = true;
        ctrl->response_ready = false;
        if (ctrl->cfg_kind == 1) {
            static const uint8_t ok_resp[2] = {0x00, 0x00};
            _s1410_set_response(ctrl, ok_resp, 2);
            ctrl->response_ready = true;
        } else if (ctrl->cfg_kind == 2) {
            // READ command parameter block finished: host now waits for bit 6
            // before issuing command 0x22.
            ctrl->phase = S1410_PHASE_IDLE;
        } else if (ctrl->cfg_kind == 4) {
            // CRT boot uses a 1-byte "drive ready" exchange and still expects
            // a 2-byte completion response via port 0x11 afterwards.
            static const uint8_t ok_resp[2] = {0x00, 0x00};
            _s1410_set_response(ctrl, ok_resp, 2);
            ctrl->response_ready = true;
        } else {
            ctrl->phase = S1410_PHASE_IDLE;
        }
    }
}

#endif
