#pragma once
/*#
    # ef9367.h

    Header-only minimal Thomson EF9367 emulator in CHIPS pin style.

    Emulated bus view (current GDP bring-up):
    - 8-bit data bus D0..D7
    - 4-bit register select A0..A3 (maps to ports 0x20..0x2F)
    - CS/RD/WR control
    - RESET pin
    - Partner board control inputs RBNK, WBNK, XORM, FM0, FM1 and SCRLM
    - vertical blank, active-low IRQ, memory-request, blanking and light-pen pins

    ## Emulated Pins

    ***************************************
    *           +-----------+             *
    * D0..D7 <->|           |<-> A0..A3   *
    *           |  EF9367   |             *
    *   CS̅  --->|   GDP     |             *
    *   RD̅  --->|           |             *
    *   WR̅  --->|           |             *
    * RESET̅ --->|           |             *
    *           +-----------+             *
    ***************************************

    - D0..D7: bidirectional data bus
    - A0..A3: register index (0x20..0x2F window)
    - CS̅/RD̅/WR̅: active-low bus control
    - RESET̅: active-low reset
    - RBNK/WBNK: display and drawing page selects
    - XORM: XOR drawing enable
    - FM0/FM1: 256/512-line format selects
    - SCRLM: active-low external scroll-latch enable
    - VBLANK: vertical-blank output
#*/
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* bus helpers: address in bits 0..3, data in bits 16..23 */
#define EF9367_GET_ADDR(p)      ((uint8_t)((p) & 0x0FULL))
#define EF9367_GET_DATA(p)      ((uint8_t)(((p) >> 16) & 0xFF))
#define EF9367_SET_DATA(p, d)   { p = ((p) & ~0xFF0000ULL) | (((uint64_t)(d) << 16) & 0xFF0000ULL); }

/* control pins */
#define EF9367_PIN_RD           (24)
#define EF9367_PIN_WR           (25)
#define EF9367_PIN_CS           (26)
#define EF9367_PIN_RESET        (27)
#define EF9367_PIN_RBNK         (28)
#define EF9367_PIN_WBNK         (29)
#define EF9367_PIN_XORM         (30)
#define EF9367_PIN_FM0          (31)
#define EF9367_PIN_FM1          (32)
#define EF9367_PIN_SCRLM        (33)
#define EF9367_PIN_VBLANK       (34)
#define EF9367_PIN_SCROLL_LOAD  (35)
#define EF9367_PIN_IRQ          (36)
#define EF9367_PIN_MW           (37)
#define EF9367_PIN_LPCK         (38)
#define EF9367_PIN_BLANK        (39)

#define EF9367_RD               (1ULL << EF9367_PIN_RD)
#define EF9367_WR               (1ULL << EF9367_PIN_WR)
#define EF9367_CS               (1ULL << EF9367_PIN_CS)
#define EF9367_RESET            (1ULL << EF9367_PIN_RESET)
#define EF9367_RBNK             (1ULL << EF9367_PIN_RBNK)
#define EF9367_WBNK             (1ULL << EF9367_PIN_WBNK)
#define EF9367_XORM             (1ULL << EF9367_PIN_XORM)
#define EF9367_FM0              (1ULL << EF9367_PIN_FM0)
#define EF9367_FM1              (1ULL << EF9367_PIN_FM1)
#define EF9367_SCRLM            (1ULL << EF9367_PIN_SCRLM)
#define EF9367_VBLANK           (1ULL << EF9367_PIN_VBLANK)
/* Board scroll latch write strobe; the byte is sampled from D0..D7. */
#define EF9367_SCROLL_LOAD      (1ULL << EF9367_PIN_SCROLL_LOAD)
#define EF9367_IRQ              (1ULL << EF9367_PIN_IRQ)
#define EF9367_MW               (1ULL << EF9367_PIN_MW)
#define EF9367_LPCK             (1ULL << EF9367_PIN_LPCK)
#define EF9367_BLANK            (1ULL << EF9367_PIN_BLANK)

typedef struct {
    uint8_t command;
    uint8_t cr1;
    uint8_t cr2;
    uint8_t ch_size;
    uint8_t dx;
    uint8_t dy;
    uint16_t x;
    uint16_t y;
    uint8_t status;
    bool ready;
    uint32_t busy_ticks;
    uint8_t ck_phase; /* EF CK phase in eighths; advances 3/8 per 4 MHz tick */
    bool expect_abs_x;
    bool expect_abs_y;
    uint8_t abs_phase; /* 0=none, 1=xh,2=xl,3=yh,4=yl on cmd-port byte stream */
    uint16_t scan_ctr;    /* exact 1.5 MHz raster phase, in CK cycles */
    bool vblank;
    bool previous_vblank;
    uint8_t irq_latches; /* STATUS bits 4..6 */
    bool lightpen_active;
    bool lightpen_force_white;
    bool lightpen_hit;
    bool previous_lpck;
    uint8_t xlp;
    uint8_t ylp;
    bool mw_request;
    bool mw_active;
    int16_t scroll_offset;
    uint8_t scroll_latch;
    uint8_t glyph_rom[96][7];
    bool glyph_rom_loaded;
    bool mode_512_lines; /* true=1024x512, false=1024x256 (each line doubled) */
    uint8_t read_bank;   /* 0/1: bank selected for scanout (RBNK) */
    uint8_t write_bank;  /* 0/1: bank selected for drawing/writes (WRNK) */
    bool xor_mode;       /* Partner board flag: XOR plot mode */
    uint8_t fb[2][(1024 * 512) / 8];
} ef9367_t;

void ef9367_init(ef9367_t *gdp);
void ef9367_reset(ef9367_t *gdp);
uint64_t ef9367_tick(ef9367_t *gdp, uint64_t pins);
/* Advance one master clock when RESET/CS/RD/WR/LPCK are inactive and the
   board inputs have already been applied with ef9367_set_board_inputs(). */
uint64_t ef9367_tick_idle(ef9367_t *gdp, uint64_t pins);
void ef9367_set_board_inputs(ef9367_t *gdp, uint64_t pins);
uint8_t ef9367_read(ef9367_t *gdp, uint8_t port);
void ef9367_write(ef9367_t *gdp, uint8_t port, uint8_t data);
void ef9367_command(ef9367_t *gdp, uint8_t cmd);
bool ef9367_load_charset_rom(ef9367_t *gdp, const uint8_t *rom, uint32_t size);
void ef9367_clear_framebuffers(ef9367_t *gdp);
bool ef9367_read_current_pixel(const ef9367_t *gdp);

#ifdef __cplusplus
}
#endif

#ifdef CHIPS_IMPL
#ifndef CHIPS_ASSERT
    #include <assert.h>
    #define CHIPS_ASSERT(c) assert(c)
#endif
static inline uint8_t _ef9367_status(const ef9367_t *gdp) {
    uint8_t st = gdp->lightpen_active ? 0x00 : 0x01;
    if (gdp->vblank) st |= 0x02;
    if (gdp->ready && (gdp->busy_ticks == 0)) {
        st |= 0x04;    /* ready */
    }
    const int logical_h = gdp->mode_512_lines ? 512 : 256;
    if (gdp->x >= 1024u || gdp->y >= (uint16_t)logical_h)
        st |= 0x08;
    st |= (uint8_t)(gdp->irq_latches & 0x70u);
    if (gdp->irq_latches & 0x70u)
        st |= 0x80;
    return st;
}

static inline uint8_t* _ef9367_fb_page(ef9367_t *gdp, uint8_t bank) {
    return gdp->fb[(bank & 1u) ? 1 : 0];
}

static inline const uint8_t* _ef9367_const_fb_page(const ef9367_t *gdp,
                                                    uint8_t bank) {
    return gdp->fb[(bank & 1u) ? 1 : 0];
}

static inline bool _ef9367_pixel_address(const ef9367_t *gdp,
                                          int x, int y,
                                          uint32_t *pixel_address) {
    const bool mode_256 = !gdp->mode_512_lines;
    const int logical_h = mode_256 ? 256 : 512;
    if (gdp->cr1 & 0x08u) {
        x &= 1023;
        y &= logical_h - 1;
    } else if ((x < 0) || (x >= 1024) || (y < 0) || (y >= logical_h)) {
        return false;
    }

    /* EF9367 logical space is bottom-left origin (x right, y up).
       1024x512 mode stores all 512 raster rows. 1024x256 mode stores only
       256 rows; vertical doubling is performed by the display path. */
    const int base_fb_y = mode_256 ? (255 - y) : (511 - y);
    const int fb_mod = mode_256 ? 256 : 512;
    const int fb_y = (base_fb_y - gdp->scroll_offset) & (fb_mod - 1);
    *pixel_address = (uint32_t)fb_y * 1024u + (uint32_t)x;
    return true;
}

static inline void _ef9367_set_px(ef9367_t *gdp, int x, int y, bool on) {
    uint32_t p = 0;
    if (!_ef9367_pixel_address(gdp, x, y, &p))
        return;
    const uint32_t byte_ix = p >> 3;
    const uint8_t bit = (uint8_t)(1u << (p & 7u));
    uint8_t *fb = _ef9367_fb_page(gdp, gdp->write_bank);
    if (on) fb[byte_ix] |= bit;
    else fb[byte_ix] &= (uint8_t)~bit;
}

static inline void _ef9367_flip_px(ef9367_t *gdp, int x, int y) {
    uint32_t p = 0;
    if (!_ef9367_pixel_address(gdp, x, y, &p))
        return;
    const uint32_t byte_ix = p >> 3;
    const uint8_t bit = (uint8_t)(1u << (p & 7u));
    uint8_t *fb = _ef9367_fb_page(gdp, gdp->write_bank);
    fb[byte_ix] ^= bit;
}

static inline void _ef9367_clear(ef9367_t *gdp) {
    memset(_ef9367_fb_page(gdp, gdp->write_bank), 0, sizeof(gdp->fb[0]));
}

static inline void _ef9367_scan_screen(ef9367_t *gdp) {
    const uint8_t value = (gdp->cr1 & 0x02u) ? 0xFFu : 0x00u;
    memset(_ef9367_fb_page(gdp, gdp->write_bank), value, sizeof(gdp->fb[0]));
}

static inline int _ef9367_p_factor(const ef9367_t *gdp) {
    const int p = (int)((gdp->ch_size >> 4) & 0x0F);
    return p ? p : 16;
}

static inline int _ef9367_q_factor(const ef9367_t *gdp) {
    const int q = (int)(gdp->ch_size & 0x0F);
    return q ? q : 16;
}

static inline void _ef9367_plot(ef9367_t *gdp, int x, int y) {
    if ((gdp->cr1 & 0x01) == 0) {
        return;
    }
    if (gdp->xor_mode) {
        /* XORM is a Partner board write mode.  It toggles every position
           plotted by the GDP; the pen/eraser selection must not suppress an
           eraser stroke.  Pen/eraser-up is still handled above. */
        _ef9367_flip_px(gdp, x, y);
        return;
    }
    _ef9367_set_px(gdp, x, y, (gdp->cr1 & 0x02) != 0);
}

static inline void _ef9367_character_point(const ef9367_t *gdp,
                                            int base_x, int base_y,
                                            int local_x, int local_y,
                                            int *px, int *py) {
    /* CTRL2 tilt is a one-dot-per-row shear. Scaling is applied first, as
       required by the datasheet, and the result is then optionally rotated
       onto the vertical writing axis. */
    if (gdp->cr2 & 0x04u)
        local_x += local_y;
    if ((gdp->cr2 & 0x08u) == 0) {
        *px = base_x + local_x;
        *py = base_y + local_y;
    } else {
        *px = base_x - local_y;
        *py = base_y + local_x;
    }
}

static inline void _ef9367_draw_block(ef9367_t *gdp, int cols, int rows) {
    const int p_factor = _ef9367_p_factor(gdp);
    const int q_factor = _ef9367_q_factor(gdp);
    const bool vertical = (gdp->cr2 & 0x08) != 0;
    const int base_x = (int)gdp->x;
    const int base_y = (int)gdp->y;
    for (int x_char = 0; x_char < cols; x_char++) {
        for (int y_char = rows - 1; y_char >= 0; y_char--) {
            for (int q = 0; q < q_factor; q++) {
                for (int p = 0; p < p_factor; p++) {
                    int px = 0, py = 0;
                    _ef9367_character_point(
                        gdp, base_x, base_y,
                        (x_char * p_factor) + p,
                        (y_char * q_factor) + q, &px, &py);
                    _ef9367_plot(gdp, px, py);
                }
            }
        }
    }

    if (!vertical) {
        gdp->x = (uint16_t)((gdp->x + cols * p_factor) & 0x0FFFu);
    } else {
        gdp->y = (uint16_t)((gdp->y + cols * p_factor) & 0x0FFFu);
    }
}

static inline void _ef9367_draw_glyph(ef9367_t *gdp, uint8_t ch) {
    if (ch < 0x20 || ch > 0x7F) {
        return;
    }
    const int p_factor = _ef9367_p_factor(gdp);
    const int q_factor = _ef9367_q_factor(gdp);
    const bool vertical = (gdp->cr2 & 0x08) != 0;
    const int gi = (int)(ch - 0x20);
    const int base_x = (int)gdp->x;
    const int base_y = (int)gdp->y;
    for (int x_char = 0; x_char < 5; x_char++) {
        for (int y_char = 7; y_char >= 0; y_char--) {
            bool set = false;
            if (y_char < 7) {
                const uint8_t bits = gdp->glyph_rom[gi][6 - y_char];
                set = (bits & (0x10 >> x_char)) != 0;
            }
            if (!set) {
                continue;
            }
            for (int q = 0; q < q_factor; q++) {
                for (int p = 0; p < p_factor; p++) {
                    int px = 0, py = 0;
                    _ef9367_character_point(
                        gdp, base_x, base_y,
                        (x_char * p_factor) + p,
                        (y_char * q_factor) + q, &px, &py);
                    _ef9367_plot(gdp, px, py);
                }
            }
        }
    }

    const int adv = 6 * p_factor;
    if (!vertical) {
        gdp->x = (uint16_t)((gdp->x + adv) & 0x0FFFu);
    } else {
        gdp->y = (uint16_t)((gdp->y + adv) & 0x0FFFu);
    }
}

static inline int _ef9367_iabs(int v) {
    return (v < 0) ? -v : v;
}

static inline bool _ef9367_vector_pattern_on(uint8_t cr2, int step) {
    const unsigned phase = (unsigned)step & 0x0Fu;
    switch (cr2 & 0x03u) {
        case 0x01: /* dotted: 2 dots on, 2 dots off */
            return (phase & 0x03u) < 2u;
        case 0x02: /* dashed: 4 dots on, 4 dots off */
            return (phase & 0x07u) < 4u;
        case 0x03: /* dash-dotted: 10 on, 2 off, 2 on, 2 off */
            return (phase < 10u) || ((phase >= 12u) && (phase < 14u));
        default: /* continuous */
            return true;
    }
}

static inline void _ef9367_draw_line_to(ef9367_t *gdp, int dst_x, int dst_y) {
    int x = (int)gdp->x;
    int y = (int)gdp->y;
    const int dx = _ef9367_iabs(dst_x - x);
    const int sx = x < dst_x ? 1 : -1;
    const int dy = -_ef9367_iabs(dst_y - y);
    const int sy = y < dst_y ? 1 : -1;
    int error = dx + dy;
    int step = 0;
    for (;;) {
        if (_ef9367_vector_pattern_on(gdp->cr2, step))
            _ef9367_plot(gdp, x, y);
        if (x == dst_x && y == dst_y)
            break;
        const int twice_error = 2 * error;
        if (twice_error >= dy) {
            error += dy;
            x += sx;
        }
        if (twice_error <= dx) {
            error += dx;
            y += sy;
        }
        ++step;
    }

    gdp->x = (uint16_t)(dst_x & 0x0FFF);
    gdp->y = (uint16_t)(dst_y & 0x0FFF);
}

static inline void _ef9367_draw_vector(ef9367_t *gdp, uint8_t cmd, int delta_x, int delta_y) {
    const int dir = cmd & 0x06;
    const bool special_axis = (cmd & 0x01) == 0;
    const int x0 = (int)gdp->x;
    const int y0 = (int)gdp->y;

    /*
        Thomson WRVECT sets DIRECT from signed (dX,dY):
            0: +X/+Y quadrant, 2: -X/+Y quadrant,
            4: +X/-Y quadrant, 6: -X/-Y quadrant.
        Special vectors (b0=0) are axis-parallel shortcuts. When either
        delta is 0, DIRECT maps to axis direction:
            0 => +X, 2 => +Y, 4 => -Y, 6 => -X.
    */
    if (special_axis) {
        switch (dir) {
            case 0x00: {
                const int len = delta_x;
                _ef9367_draw_line_to(gdp, x0 + len, y0);
                return;
            }
            case 0x02: {
                const int len = delta_y;
                _ef9367_draw_line_to(gdp, x0, y0 + len);
                return;
            }
            case 0x04: {
                const int len = delta_y;
                _ef9367_draw_line_to(gdp, x0, y0 - len);
                return;
            }
            case 0x06: {
                const int len = delta_x;
                _ef9367_draw_line_to(gdp, x0 - len, y0);
                return;
            }
            default:
                return;
        }
    }

    /* Oblique vectors (b0=1) use both dX and dY with quadrant signs. */
    int sx = 1;
    int sy = 1;
    switch (dir) {
        case 0x00: sx = +1; sy = +1; break;
        case 0x02: sx = -1; sy = +1; break;
        case 0x04: sx = +1; sy = -1; break;
        case 0x06: sx = -1; sy = -1; break;
        default: break;
    }
    const int dst_x = x0 + (sx * delta_x);
    const int dst_y = y0 + (sy * delta_y);
    _ef9367_draw_line_to(gdp, dst_x, dst_y);
}

/*
    The Partner master tick is one 4 MHz Z80 T-state. The EF9367 CK period is
    667 ns in the datasheet's typical application, exactly 8/3 Partner ticks
    for the board's nominal 24 MHz crystal. Round up so READY is never
    asserted before the modeled CK cycle has completed.
*/
static inline uint32_t _ef9367_ck_to_busy_ticks(const ef9367_t *gdp,
                                                uint32_t ck_ticks) {
    /* The board derives the 1.5 MHz EF clock by dividing its 24 MHz video
       crystal by 16. Preserve the phase between that clock and the 4 MHz
       Partner master clock instead of independently rounding every cycle. */
    const uint32_t eighths = ck_ticks * 8u - (uint32_t)gdp->ck_phase;
    return (eighths + 2u) / 3u;
}

static inline int _ef9367_vector_steps(uint8_t cmd, int delta_x, int delta_y) {
    if ((cmd & 0x01u) == 0u) {
        switch (cmd & 0x06u) {
            case 0x00:
            case 0x06:
                return _ef9367_iabs(delta_x);
            default:
                return _ef9367_iabs(delta_y);
        }
    }
    const int abs_x = _ef9367_iabs(delta_x);
    const int abs_y = _ef9367_iabs(delta_y);
    return (abs_x > abs_y) ? abs_x : abs_y;
}

static inline uint32_t _ef9367_vector_busy_ticks(const ef9367_t *gdp,
                                                  uint8_t cmd,
                                                  int delta_x,
                                                  int delta_y) {
    /* The plotting diagram shows 2 CK of synchronization, 2 CK of
       initialization, then one CK for each component dot including both
       endpoints. */
    const uint32_t dots =
        (uint32_t)_ef9367_vector_steps(cmd, delta_x, delta_y) + 1u;
    return _ef9367_ck_to_busy_ticks(gdp, 4u + dots);
}

static inline uint32_t _ef9367_character_busy_ticks(const ef9367_t *gdp) {
    const uint32_t p = (uint32_t)_ef9367_p_factor(gdp);
    const uint32_t q = (uint32_t)_ef9367_q_factor(gdp);
    return _ef9367_ck_to_busy_ticks(gdp, 48u * p * q); /* 6P x 8Q CK */
}

static inline uint32_t _ef9367_block_busy_ticks(const ef9367_t *gdp,
                                                 uint32_t cols,
                                                 uint32_t rows) {
    const uint32_t p = (uint32_t)_ef9367_p_factor(gdp);
    const uint32_t q = (uint32_t)_ef9367_q_factor(gdp);
    return _ef9367_ck_to_busy_ticks(gdp, cols * p * rows * q);
}

static inline uint32_t _ef9367_clear_busy_ticks(const ef9367_t *gdp) {
    /* Partner ties FMAT to CK for 525-line synchronization. A field is 262.5
       lines of 96 CK (25,200 CK), and the screen scan begins at the next VB
       falling edge before scanning one non-interlaced field or two
       interlaced fields. */
    const uint32_t field_ck = 25200u;
    const uint32_t vb_falling_ck = 36u * 96u;
    const uint32_t until_vb_falling = (gdp->scan_ctr < vb_falling_ck)
        ? (vb_falling_ck - (uint32_t)gdp->scan_ctr)
        : (field_ck - (uint32_t)gdp->scan_ctr + vb_falling_ck);
    const uint32_t fields = gdp->mode_512_lines ? 2u : 1u;
    return _ef9367_ck_to_busy_ticks(gdp, until_vb_falling + fields * field_ck);
}

void ef9367_init(ef9367_t *gdp) {
    CHIPS_ASSERT(gdp);
    memset(gdp, 0, sizeof(*gdp));
    ef9367_reset(gdp);
}

void ef9367_reset(ef9367_t *gdp) {
    CHIPS_ASSERT(gdp);
    gdp->command = 0;
    gdp->cr1 = 0;
    gdp->cr2 = 0;
    gdp->ch_size = 0x21;
    gdp->dx = 0;
    gdp->dy = 0;
    gdp->x = 0;
    gdp->y = 0;
    gdp->ready = true;
    gdp->busy_ticks = 0;
    gdp->ck_phase = 0;
    gdp->expect_abs_x = false;
    gdp->expect_abs_y = false;
    gdp->abs_phase = 0;
    gdp->scan_ctr = 0;
    gdp->vblank = true;
    gdp->previous_vblank = true;
    gdp->irq_latches = 0;
    gdp->lightpen_active = false;
    gdp->lightpen_force_white = false;
    gdp->lightpen_hit = false;
    gdp->previous_lpck = false;
    gdp->xlp = 0;
    gdp->ylp = 0;
    gdp->mw_request = false;
    gdp->mw_active = false;
    gdp->scroll_offset = 0;
    gdp->scroll_latch = 0;
    gdp->glyph_rom_loaded = false;
    gdp->mode_512_lines = true;
    gdp->read_bank = 0;
    gdp->write_bank = 0;
    gdp->xor_mode = false;
    gdp->status = _ef9367_status(gdp);
}

void ef9367_clear_framebuffers(ef9367_t *gdp) {
    CHIPS_ASSERT(gdp);
    memset(gdp->fb, 0, sizeof(gdp->fb));
}

bool ef9367_read_current_pixel(const ef9367_t *gdp) {
    CHIPS_ASSERT(gdp);
    uint32_t p = 0;
    if (!_ef9367_pixel_address(gdp, (int)gdp->x, (int)gdp->y, &p))
        return false;
    const uint8_t *fb = _ef9367_const_fb_page(gdp, gdp->write_bank);
    return (fb[p >> 3] & (uint8_t)(1u << (p & 7u))) != 0u;
}

static inline uint8_t _ef9367_read_idx(ef9367_t *gdp, uint8_t idx) {
    switch (idx & 0x0F) {
        case 0x0: { /* status; this address acknowledges IRQ latches */
            const uint8_t status = _ef9367_status(gdp);
            gdp->irq_latches = 0;
            gdp->status = _ef9367_status(gdp);
            return status;
        }
        case 0xF: return _ef9367_status(gdp); /* non-destructive status */
        case 0x1: return gdp->cr1 & 0x7Fu;
        case 0x2: return gdp->cr2 & 0x0Fu;
        case 0x3: return gdp->ch_size;
        case 0x5: return gdp->dx;
        case 0x7: return gdp->dy;
        case 0x8: return (uint8_t)((gdp->x >> 8) & 0x0Fu);
        case 0x9: return (uint8_t)(gdp->x & 0xFF);
        case 0xA: return (uint8_t)((gdp->y >> 8) & 0x0Fu);
        case 0xB: return (uint8_t)(gdp->y & 0xFF);
        case 0xC: {
            const uint8_t data = (uint8_t)((gdp->xlp & 0xFCu) |
                                           (gdp->lightpen_hit ? 1u : 0u));
            gdp->lightpen_hit = false;
            return data;
        }
        case 0xD: {
            const uint8_t data = gdp->ylp;
            gdp->lightpen_hit = false;
            return data;
        }
        default:  return 0xFF;
    }
}

static inline void _ef9367_write_idx(ef9367_t *gdp, uint8_t idx, uint8_t data) {
    switch (idx & 0x0F) {
        case 0x0:
            /* EF936x command space: 0x00..0x1F and 0x80..0xFF.
               0x20..0x7F are printable character codes. */
            if ((data < 0x20) || (data >= 0x80)) {
                ef9367_command(gdp, data);
            } else {
                _ef9367_draw_glyph(gdp, data);
                gdp->busy_ticks = _ef9367_character_busy_ticks(gdp);
            }
            break;
        case 0x1: gdp->cr1 = data & 0x7Fu; break;
        case 0x2: gdp->cr2 = data & 0x0Fu; break;
        case 0x3: gdp->ch_size = data; break;
        case 0x5: gdp->dx = data; break;
        case 0x7: gdp->dy = data; break;
        case 0x8: gdp->x = (uint16_t)((gdp->x & 0x00FFu) | ((uint16_t)(data & 0x0Fu) << 8)); break;
        case 0x9: gdp->x = (uint16_t)((gdp->x & 0xFF00u) | data); break;
        case 0xA: gdp->y = (uint16_t)((gdp->y & 0x00FFu) | ((uint16_t)(data & 0x0Fu) << 8)); break;
        case 0xB: gdp->y = (uint16_t)((gdp->y & 0xFF00u) | data); break;
        default: break;
    }
    gdp->ready = !gdp->mw_request && !gdp->mw_active;
    gdp->status = _ef9367_status(gdp);
}

void ef9367_set_board_inputs(ef9367_t *gdp, uint64_t pins) {
    CHIPS_ASSERT(gdp);
    /* These are physical Partner GDP board inputs, sampled continuously by
       the controller rather than copied into it by motherboard code. */
    gdp->read_bank = (pins & EF9367_RBNK) ? 1u : 0u;
    gdp->write_bank = (pins & EF9367_WBNK) ? 1u : 0u;
    gdp->xor_mode = (pins & EF9367_XORM) != 0;
    const uint8_t format = (uint8_t)(((pins & EF9367_FM0) ? 1u : 0u) |
                                     ((pins & EF9367_FM1) ? 2u : 0u));
    if (format == 0u) {
        gdp->mode_512_lines = false;
    } else if (format == 3u) {
        gdp->mode_512_lines = true;
    } else {
        /* Mixed values occur while the two PIO outputs settle. */
        gdp->mode_512_lines = (format & 1u) != 0u;
    }
    if (pins & EF9367_SCROLL_LOAD) {
        gdp->scroll_latch = EF9367_GET_DATA(pins);
    }
    gdp->scroll_offset = (pins & EF9367_SCRLM) ? 0 : (int8_t)gdp->scroll_latch;
}

static inline bool _ef9367_memory_cycle_free(const ef9367_t *gdp) {
    /* The 525-line timing diagram is 96 CK per line. ALL is low during the
       64-cycle memory window beginning after 23 CK. In normal write mode
       that window is occupied by display on lines 36..243 and by the three
       four-line vertical-blank refresh periods shown in the datasheet. */
    const uint16_t line = (uint16_t)(gdp->scan_ctr / 96u);
    const uint16_t phase = (uint16_t)(gdp->scan_ctr % 96u);
    if ((phase < 23u) || (phase >= 87u))
        return true;

    const bool refresh = ((line >= 10u) && (line <= 13u)) ||
                         ((line >= 26u) && (line <= 29u)) ||
                         ((line >= 248u) && (line <= 251u));
    const bool display = ((gdp->cr1 & 0x04u) == 0u) &&
                         (line >= 36u) && (line <= 243u);
    return !refresh && !display;
}

static inline void _ef9367_advance_master_tick(ef9367_t *gdp, bool lpck) {
    /* Advance exactly one 4 MHz Partner master tick. EF CK is QD from the
       board's 24 MHz video-clock counter, so it advances three times per
       eight Partner ticks. The 525-line field contains 25,200 EF CK cycles. */
    const bool was_ready = gdp->ready && (gdp->busy_ticks == 0);
    if (gdp->busy_ticks)
        gdp->busy_ticks--;
    gdp->ck_phase = (uint8_t)(gdp->ck_phase + 3u);
    bool ck_edge = false;
    if (gdp->ck_phase >= 8u) {
        gdp->ck_phase = (uint8_t)(gdp->ck_phase - 8u);
        ck_edge = true;
        gdp->scan_ctr++;
        if (gdp->scan_ctr >= 25200u)
            gdp->scan_ctr = 0;
    }

    /* The active 208-line window begins at VB's falling edge on line 36.
       VB rises again after line 243 and remains high across field wrap. */
    gdp->previous_vblank = gdp->vblank;
    gdp->vblank = (gdp->scan_ctr < (36u * 96u)) ||
                  (gdp->scan_ctr >= (244u * 96u));

    const bool vblank_rising = gdp->vblank && !gdp->previous_vblank;
    if (vblank_rising && (gdp->cr1 & 0x20u))
        gdp->irq_latches |= 0x20u;

    const bool lpck_rising = lpck && !gdp->previous_lpck;
    gdp->previous_lpck = lpck;
    if (gdp->lightpen_active && (lpck_rising || vblank_rising)) {
        if (lpck_rising && !gdp->vblank) {
            uint16_t segment = (uint16_t)(gdp->scan_ctr % 96u);
            if (segment > 63u)
                segment = 63u;
            const uint16_t line = (uint16_t)(gdp->scan_ctr / 96u);
            gdp->xlp = (uint8_t)(segment << 2);
            gdp->ylp = (uint8_t)((line - 36u) & 0xFFu);
            gdp->lightpen_hit = true;
        } else {
            gdp->lightpen_hit = false;
        }
        gdp->lightpen_active = false;
        gdp->lightpen_force_white = false;
        if (gdp->cr1 & 0x10u)
            gdp->irq_latches |= 0x10u;
    }

    /* Command 0F requests exactly one next-free display-memory cycle. MW is
       low for that complete CK. READY rises only when the cycle ends, which
       is the completion handshake used by the Partner CGRAF pixel reader. */
    if (ck_edge) {
        if (gdp->mw_active) {
            gdp->mw_active = false;
        } else if (gdp->mw_request && _ef9367_memory_cycle_free(gdp)) {
            gdp->mw_request = false;
            gdp->mw_active = true;
        }
    }
    gdp->ready = !gdp->mw_request && !gdp->mw_active;
    const bool is_ready = gdp->ready && (gdp->busy_ticks == 0);
    if (!was_ready && is_ready && (gdp->cr1 & 0x40u))
        gdp->irq_latches |= 0x40u;
    gdp->status = _ef9367_status(gdp);
}

static inline uint64_t _ef9367_output_pins(const ef9367_t *gdp,
                                            uint64_t pins) {
    if (gdp->vblank) {
        pins |= EF9367_VBLANK;
    } else {
        pins &= ~EF9367_VBLANK;
    }
    /* IRQ and MW are open-drain/active-low outputs. */
    if (gdp->irq_latches & 0x70u)
        pins &= ~EF9367_IRQ;
    else
        pins |= EF9367_IRQ;
    if (gdp->mw_active || (gdp->lightpen_active && gdp->lightpen_force_white))
        pins &= ~EF9367_MW;
    else
        pins |= EF9367_MW;
    if (gdp->vblank || (gdp->cr1 & 0x04u))
        pins |= EF9367_BLANK;
    else
        pins &= ~EF9367_BLANK;
    return pins;
}

uint64_t ef9367_tick(ef9367_t *gdp, uint64_t pins) {
    CHIPS_ASSERT(gdp);

    if ((pins & EF9367_RESET) == 0) {
        ef9367_reset(gdp);
        pins |= EF9367_IRQ | EF9367_MW | EF9367_BLANK | EF9367_VBLANK;
        return pins;
    }

    _ef9367_advance_master_tick(gdp, (pins & EF9367_LPCK) != 0);
    ef9367_set_board_inputs(gdp, pins);

    /* active-low CS/RD/WR */
    if ((pins & EF9367_CS) == 0) {
        const uint8_t idx = EF9367_GET_ADDR(pins);
        if ((pins & EF9367_RD) == 0) {
            EF9367_SET_DATA(pins, _ef9367_read_idx(gdp, idx));
        } else if ((pins & EF9367_WR) == 0) {
            _ef9367_write_idx(gdp, idx, EF9367_GET_DATA(pins));
        }
    }

    gdp->ready = !gdp->mw_request && !gdp->mw_active;
    gdp->status = _ef9367_status(gdp);
    return _ef9367_output_pins(gdp, pins);
}

uint64_t ef9367_tick_idle(ef9367_t *gdp, uint64_t pins) {
    CHIPS_ASSERT(gdp);
    CHIPS_ASSERT((pins & (EF9367_RESET | EF9367_CS | EF9367_RD | EF9367_WR)) ==
                 (EF9367_RESET | EF9367_CS | EF9367_RD | EF9367_WR));
    CHIPS_ASSERT((pins & EF9367_LPCK) == 0u);
    _ef9367_advance_master_tick(gdp, false);
    return _ef9367_output_pins(gdp, pins);
}

uint8_t ef9367_read(ef9367_t *gdp, uint8_t port) {
    CHIPS_ASSERT(gdp);
    if (port < 0x20 || port > 0x2F) {
        return 0xFF;
    }
    return _ef9367_read_idx(gdp, (uint8_t)(port - 0x20));
}

void ef9367_command(ef9367_t *gdp, uint8_t cmd) {
    CHIPS_ASSERT(gdp);
    gdp->command = cmd;
    uint32_t busy_ticks = _ef9367_ck_to_busy_ticks(gdp, 2u);

    /* Small vectors (bit7=1): packed dX/dY in 2-bit fields. */
    if ((cmd & 0x80) != 0) {
        const int delta_x = (int)((cmd >> 5) & 0x03);
        const int delta_y = (int)((cmd >> 3) & 0x03);
        busy_ticks = _ef9367_vector_busy_ticks(gdp, cmd, delta_x, delta_y);
        _ef9367_draw_vector(gdp, cmd, delta_x, delta_y);
    }
    /* Standard vectors (10h..1Fh): 18h..1Fh replace the smaller delta by
       the larger one, yielding the documented axis/diagonal shortcuts. */
    else if ((cmd & 0xF0) == 0x10) {
        int delta_x = (int)gdp->dx;
        int delta_y = (int)gdp->dy;
        if (cmd & 0x08u) {
            const int length = delta_x > delta_y ? delta_x : delta_y;
            delta_x = length;
            delta_y = length;
        }
        busy_ticks = _ef9367_vector_busy_ticks(
            gdp, cmd, delta_x, delta_y);
        _ef9367_draw_vector(gdp, cmd, delta_x, delta_y);
    }
    else switch (cmd) {
        case 0x04: /* clear current page */
            _ef9367_clear(gdp);
            busy_ticks = _ef9367_clear_busy_ticks(gdp);
            break;
        case 0x00: /* pen selection */
            gdp->cr1 |= 0x02;
            break;
        case 0x01: /* eraser selection */
            gdp->cr1 &= (uint8_t)~0x02;
            break;
        case 0x02: /* pen/eraser down */
            gdp->cr1 |= 0x01;
            break;
        case 0x03: /* pen/eraser up */
            gdp->cr1 &= (uint8_t)~0x01;
            break;
        case 0x05: /* X and Y registers reset */
            gdp->x = 0;
            gdp->y = 0;
            break;
        case 0x06: /* CLS + X=Y=0 */
            _ef9367_clear(gdp);
            gdp->x = 0;
            gdp->y = 0;
            gdp->irq_latches = 0;
            gdp->lightpen_active = false;
            gdp->lightpen_force_white = false;
            gdp->lightpen_hit = false;
            gdp->mw_request = false;
            gdp->mw_active = false;
            busy_ticks = _ef9367_clear_busy_ticks(gdp);
            break;
        case 0x08: /* light-pen sequence, WHITE forced through MW/ALL */
        case 0x09: /* light-pen sequence without WHITE */
            gdp->lightpen_active = true;
            gdp->lightpen_force_white = cmd == 0x08;
            gdp->lightpen_hit = false;
            busy_ticks = 0;
            break;
        case 0x07: /* clear, min size, reset other registers */
            _ef9367_clear(gdp);
            gdp->ch_size = 0x11;
            gdp->cr1 = 0;
            gdp->cr2 = 0;
            gdp->dx = 0;
            gdp->dy = 0;
            gdp->x = 0;
            gdp->y = 0;
            busy_ticks = _ef9367_clear_busy_ticks(gdp);
            break;
        case 0x0A: /* EF9367 solid 5P x 8Q character block */
            _ef9367_draw_block(gdp, 5, 8);
            /* Like every 5x8 character, the next origin is one P-wide
               spacing column beyond the solid block. */
            if ((gdp->cr2 & 0x08u) == 0u)
                gdp->x = (uint16_t)((gdp->x + _ef9367_p_factor(gdp)) & 0x0FFFu);
            else
                gdp->y = (uint16_t)((gdp->y + _ef9367_p_factor(gdp)) & 0x0FFFu);
            busy_ticks = _ef9367_character_busy_ticks(gdp);
            break;
        case 0x0B: /* 4x4 block */
            _ef9367_draw_block(gdp, 4, 4);
            busy_ticks = _ef9367_block_busy_ticks(gdp, 4u, 4u);
            break;
        case 0x0C: /* scan the screen with selected pen or eraser */
            _ef9367_scan_screen(gdp);
            busy_ticks = _ef9367_clear_busy_ticks(gdp);
            break;
        case 0x0D: /* X register reset to 0 */
            gdp->x = 0;
            break;
        case 0x0E: /* Y register reset to 0 */
            gdp->y = 0;
            break;
        case 0x0F: /* next-free-cycle display-memory request */
            gdp->mw_request = true;
            break;
        default:
            break;
    }
    gdp->busy_ticks = busy_ticks;
    gdp->ready = !gdp->mw_request && !gdp->mw_active;
    gdp->status = _ef9367_status(gdp);
}

void ef9367_write(ef9367_t *gdp, uint8_t port, uint8_t data) {
    CHIPS_ASSERT(gdp);
    if (port < 0x20 || port > 0x2F) {
        return;
    }
    _ef9367_write_idx(gdp, (uint8_t)(port - 0x20), data);
}

bool ef9367_load_charset_rom(ef9367_t *gdp, const uint8_t *rom, uint32_t size) {
    CHIPS_ASSERT(gdp);
    if (!rom || size < 480) {
        return false;
    }
    for (int ch = 0; ch < 96; ch++) {
        const uint8_t *src = rom + (ch * 5);
        uint64_t bits = 0;
        for (int b = 0; b < 5; b++) {
            bits = (bits << 8) | src[b];
        }
        for (int r = 0; r < 7; r++) {
            const int shift = 35 - r * 5;
            gdp->glyph_rom[ch][r] = (uint8_t)((bits >> shift) & 0x1F);
        }
    }
    gdp->glyph_rom_loaded = true;
    return true;
}
#endif
