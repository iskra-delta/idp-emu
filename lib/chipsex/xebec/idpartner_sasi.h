#pragma once
/*#
    # idpartner_sasi.h

    Iskra Delta Partner SASI adapter.

    This models the board-side adapter that maps CPU ports 0x10..0x12 onto a
    SASI target. It follows the same split as MAME's Partner SASI card:
    - port 0x10: bus status / control bits
    - port 0x11: data transfers
    - port 0x12: reset
    - DRQ is generated from REQ when enabled by the control register
#*/
#include <stdint.h>
#include <stdbool.h>
#include "s1410.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    s1410_t *target;
    bool session_active;
    bool sel_latched;
    bool data_enable;
    bool drq_enable;
    bool drq;
    uint8_t last_ctrl;
} idpartner_sasi_t;

void idpartner_sasi_init(idpartner_sasi_t *adp, s1410_t *target);
void idpartner_sasi_reset(idpartner_sasi_t *adp);
uint8_t idpartner_sasi_status_r(idpartner_sasi_t *adp);
uint8_t idpartner_sasi_data_r(idpartner_sasi_t *adp);
void idpartner_sasi_ctrl_w(idpartner_sasi_t *adp, uint8_t data);
void idpartner_sasi_data_w(idpartner_sasi_t *adp, uint8_t data);
void idpartner_sasi_reset_w(idpartner_sasi_t *adp, uint8_t data);
bool idpartner_sasi_drq(const idpartner_sasi_t *adp);

#ifdef __cplusplus
}
#endif

#ifdef CHIPS_IMPL
#ifndef CHIPS_ASSERT
    #include <assert.h>
    #define CHIPS_ASSERT(c) assert(c)
#endif

static inline bool _idpartner_sasi_req(const idpartner_sasi_t *adp) {
    if (!adp->target || !adp->target->present) {
        return false;
    }
    switch (adp->target->phase) {
        case S1410_PHASE_COMMAND:
            return true;
        case S1410_PHASE_READ_DATA:
            return adp->target->data_idx < adp->target->data_len;
        case S1410_PHASE_WRITE_DATA:
            return adp->target->data_idx < adp->target->data_len;
        case S1410_PHASE_STATUS:
        case S1410_PHASE_MESSAGE:
            return true;
        default:
            return false;
    }
}

/* DRQ only gates the Z80 DMA data phase. The Partner firmware consumes the
   post-transfer response bytes through PIO in the HD ISR; asserting DRQ during
   RESPONSE (or command/config phases) lets DMA swallow those bytes and leaves
   the ISR polling forever for REQ+IO+CD. */
static inline bool _idpartner_sasi_drq_ready(const idpartner_sasi_t *adp) {
    if (!adp->target || !adp->target->present) {
        return false;
    }
    switch (adp->target->phase) {
        case S1410_PHASE_READ_DATA:
            return adp->target->data_idx < adp->target->data_len;
        case S1410_PHASE_WRITE_DATA:
            return adp->target->data_idx < adp->target->data_len;
        default:
            return false;
    }
}

static inline bool _idpartner_sasi_io(const idpartner_sasi_t *adp) {
    if (!adp->target) {
        return false;
    }
    return (adp->target->phase == S1410_PHASE_READ_DATA) ||
           (adp->target->phase == S1410_PHASE_STATUS) ||
           (adp->target->phase == S1410_PHASE_MESSAGE);
}

static inline bool _idpartner_sasi_cd(const idpartner_sasi_t *adp) {
    if (!adp->target) {
        return false;
    }
    return (adp->target->phase == S1410_PHASE_COMMAND) ||
           (adp->target->phase == S1410_PHASE_STATUS) ||
           (adp->target->phase == S1410_PHASE_MESSAGE);
}

static inline bool _idpartner_sasi_msg(const idpartner_sasi_t *adp) {
    return adp->target && adp->target->phase == S1410_PHASE_MESSAGE;
}

static inline bool _idpartner_sasi_bsy(const idpartner_sasi_t *adp) {
    if (!adp->target || !adp->target->present) {
        return false;
    }
    return adp->target->busy;
}

static inline void _idpartner_sasi_update_drq(idpartner_sasi_t *adp) {
    adp->drq = adp->data_enable && adp->drq_enable && _idpartner_sasi_drq_ready(adp);
}

void idpartner_sasi_init(idpartner_sasi_t *adp, s1410_t *target) {
    CHIPS_ASSERT(adp);
    adp->target = target;
    idpartner_sasi_reset(adp);
}

void idpartner_sasi_reset(idpartner_sasi_t *adp) {
    CHIPS_ASSERT(adp);
    adp->session_active = false;
    adp->sel_latched = false;
    adp->data_enable = false;
    adp->drq_enable = false;
    adp->drq = false;
    adp->last_ctrl = 0;
}

uint8_t idpartner_sasi_status_r(idpartner_sasi_t *adp) {
    CHIPS_ASSERT(adp);
    if (!adp->target || !adp->target->present) {
        /* No controller on the bus: the data lines float high. */
        return 0xFF;
    }
    uint8_t data = 0;
    if (_idpartner_sasi_req(adp)) data |= 0x80;
    if (_idpartner_sasi_io(adp))  data |= 0x40;
    if (_idpartner_sasi_msg(adp)) data |= 0x20;
    if (_idpartner_sasi_cd(adp))  data |= 0x10;
    if (_idpartner_sasi_bsy(adp)) data |= 0x08;
    _idpartner_sasi_update_drq(adp);
    return data;
}

uint8_t idpartner_sasi_data_r(idpartner_sasi_t *adp) {
    CHIPS_ASSERT(adp);
    if (!adp->target || !adp->target->present) {
        /* No controller on the bus: the data lines float high. */
        return 0xFF;
    }
    const uint8_t data = s1410_read_data(adp->target);
    _idpartner_sasi_update_drq(adp);
    return data;
}

void idpartner_sasi_ctrl_w(idpartner_sasi_t *adp, uint8_t data) {
    CHIPS_ASSERT(adp);
    if (!adp->target || !adp->target->present) {
        return;
    }

    const bool sel = (data & 0x01) != 0;
    adp->data_enable = (data & 0x02) != 0;
    adp->drq_enable = (data & 0x20) != 0;
    adp->last_ctrl = data;

    /* hd_end_session$ drops every control bit; mirror that by releasing the
       target bus and clearing adapter session state instead of leaving the
       previous SASI phase latched for the next transfer. */
    if (data == 0x00) {
        if (adp->session_active) {
            s1410_write_control(adp->target, 0x00);
        }
        adp->session_active = false;
        adp->sel_latched = false;
        adp->drq = false;
        return;
    }

    /* Every hd_begin_session$ asserts SEL even if a prior session left
       sel_latched set; re-drive the target select pulse so BSY is visible
       to hd_wait_bsy$ on back-to-back kernel transfers. */
    if (sel) {
        adp->session_active = true;
        s1410_write_control(adp->target, 0x01);
    }
    if (!sel && adp->sel_latched && adp->session_active && adp->data_enable) {
        s1410_write_control(adp->target, 0x02);
    }
    adp->sel_latched = sel;

    _idpartner_sasi_update_drq(adp);
}

void idpartner_sasi_data_w(idpartner_sasi_t *adp, uint8_t data) {
    CHIPS_ASSERT(adp);
    if (!adp->target) {
        return;
    }
    s1410_write_data(adp->target, data);
    _idpartner_sasi_update_drq(adp);
}

void idpartner_sasi_reset_w(idpartner_sasi_t *adp, uint8_t data) {
    (void)data;
    CHIPS_ASSERT(adp);
    if (!adp->target) {
        return;
    }
    s1410_reset(adp->target);
    idpartner_sasi_reset(adp);
}

bool idpartner_sasi_drq(const idpartner_sasi_t *adp) {
    CHIPS_ASSERT(adp);
    return adp->drq;
}

#endif
