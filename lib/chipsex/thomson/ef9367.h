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
    - vertical-blank output

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

typedef struct {
    uint8_t command;
    uint8_t cr1;
    uint8_t cr2;
    uint8_t ch_size;
    uint8_t dx;
    uint8_t dy;
    uint16_t x;
    uint16_t y;
    uint8_t status;   /* bit1=vblank, bit2=ready */
    bool ready;
    uint16_t busy_ticks;
    bool expect_abs_x;
    bool expect_abs_y;
    uint8_t abs_phase; /* 0=none, 1=xh,2=xl,3=yh,4=yl on cmd-port byte stream */
    uint16_t scan_ctr;
    bool vblank;
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
uint8_t ef9367_read(ef9367_t *gdp, uint8_t port);
void ef9367_write(ef9367_t *gdp, uint8_t port, uint8_t data);
void ef9367_command(ef9367_t *gdp, uint8_t cmd);
bool ef9367_load_charset_rom(ef9367_t *gdp, const uint8_t *rom, uint32_t size);
void ef9367_clear_framebuffers(ef9367_t *gdp);

#ifdef __cplusplus
}
#endif

#ifdef CHIPS_IMPL
#ifndef CHIPS_ASSERT
    #include <assert.h>
    #define CHIPS_ASSERT(c) assert(c)
#endif
#include <math.h>

static inline uint8_t _ef9367_status(const ef9367_t *gdp) {
    uint8_t st = gdp->vblank ? 0x02 : 0x00;
    if (gdp->ready && (gdp->busy_ticks == 0)) {
        st |= 0x04;    /* ready */
    }
    return st;
}

static inline uint8_t* _ef9367_fb_page(ef9367_t *gdp, uint8_t bank) {
    return gdp->fb[(bank & 1u) ? 1 : 0];
}

static inline void _ef9367_set_px(ef9367_t *gdp, int x, int y, bool on) {
    const bool mode_256 = !gdp->mode_512_lines;
    const int logical_h = mode_256 ? 256 : 512;
    if ((x < 0) || (x >= 1024) || (y < 0) || (y >= logical_h)) {
        return;
    }
    /* EF9367 logical space is bottom-left origin (x right, y up).
       1024x512 mode stores all 512 raster rows.
       1024x256 mode stores only 256 rows (32KB); scanout stretch is external. */
    const int base_fb_y = mode_256 ? (255 - y) : (511 - y);
    const int fb_mod = mode_256 ? 256 : 512;
    const int fb_y = (base_fb_y - gdp->scroll_offset) & (fb_mod - 1);
    const uint32_t p = (uint32_t)fb_y * 1024u + (uint32_t)x;
    const uint32_t byte_ix = p >> 3;
    const uint8_t bit = (uint8_t)(1u << (p & 7u));
    uint8_t *fb = _ef9367_fb_page(gdp, gdp->write_bank);
    if (on) fb[byte_ix] |= bit;
    else fb[byte_ix] &= (uint8_t)~bit;
}

static inline void _ef9367_flip_px(ef9367_t *gdp, int x, int y) {
    const bool mode_256 = !gdp->mode_512_lines;
    const int logical_h = mode_256 ? 256 : 512;
    if ((x < 0) || (x >= 1024) || (y < 0) || (y >= logical_h)) {
        return;
    }
    const int base_fb_y = mode_256 ? (255 - y) : (511 - y);
    const int fb_mod = mode_256 ? 256 : 512;
    const int fb_y = (base_fb_y - gdp->scroll_offset) & (fb_mod - 1);
    const uint32_t p = (uint32_t)fb_y * 1024u + (uint32_t)x;
    const uint32_t byte_ix = p >> 3;
    const uint8_t bit = (uint8_t)(1u << (p & 7u));
    uint8_t *fb = _ef9367_fb_page(gdp, gdp->write_bank);
    fb[byte_ix] ^= bit;
}

static inline void _ef9367_clear(ef9367_t *gdp) {
    memset(_ef9367_fb_page(gdp, gdp->write_bank), 0, sizeof(gdp->fb[0]));
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
        /* Partner XOR mode toggles pixels only when "pen" is selected. */
        if ((gdp->cr1 & 0x02) != 0) {
            _ef9367_flip_px(gdp, x, y);
        }
        return;
    }
    _ef9367_set_px(gdp, x, y, (gdp->cr1 & 0x02) != 0);
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
                    int px = 0;
                    int py = 0;
                    if (!vertical) {
                        px = base_x + (x_char * p_factor) + p;
                        py = base_y + (y_char * q_factor) + q;
                    } else {
                        px = base_x - ((y_char * q_factor) + q);
                        py = base_y + ((x_char * p_factor) + p);
                    }
                    _ef9367_plot(gdp, px, py);
                }
            }
        }
    }

    if (!vertical) {
        gdp->x = (uint16_t)(gdp->x + cols * p_factor);
    } else {
        gdp->y = (uint16_t)(gdp->y + cols * p_factor);
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
                    int px = 0;
                    int py = 0;
                    if (!vertical) {
                        px = base_x + (x_char * p_factor) + p;
                        py = base_y + (y_char * q_factor) + q;
                    } else {
                        px = base_x - ((y_char * q_factor) + q);
                        py = base_y + ((x_char * p_factor) + p);
                    }
                    _ef9367_plot(gdp, px, py);
                }
            }
        }
    }

    const int adv = 6 * p_factor;
    if (!vertical) {
        gdp->x = (uint16_t)(gdp->x + adv);
    } else {
        gdp->y = (uint16_t)(gdp->y + adv);
    }
}

static inline int _ef9367_iabs(int v) {
    return (v < 0) ? -v : v;
}

static inline void _ef9367_draw_line_to(ef9367_t *gdp, int dst_x, int dst_y) {
    const int x0 = (int)gdp->x;
    const int y0 = (int)gdp->y;
    const int delta_x = dst_x - x0;
    const int delta_y = dst_y - y0;
    const int steps = _ef9367_iabs(delta_x) > _ef9367_iabs(delta_y)
        ? _ef9367_iabs(delta_x)
        : _ef9367_iabs(delta_y);

    if (steps == 0) {
        _ef9367_plot(gdp, x0, y0);
        gdp->x = (uint16_t)dst_x;
        gdp->y = (uint16_t)dst_y;
        return;
    }

    const double x_step = (double)delta_x / (double)steps;
    const double y_step = (double)delta_y / (double)steps;
    double x = (double)x0;
    double y = (double)y0;
    for (int i = 0; i <= steps; i++) {
        const int px = (int)nearbyint(x);
        const int py = (int)nearbyint(y);
        _ef9367_plot(gdp, px, py);
        x += x_step;
        y += y_step;
    }

    gdp->x = (uint16_t)dst_x;
    gdp->y = (uint16_t)dst_y;
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
    gdp->expect_abs_x = false;
    gdp->expect_abs_y = false;
    gdp->abs_phase = 0;
    gdp->scan_ctr = 0;
    gdp->vblank = false;
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

static inline uint8_t _ef9367_read_idx(ef9367_t *gdp, uint8_t idx) {
    switch (idx & 0x0F) {
        case 0x0: /* status at command port */
        case 0xF: /* status NI */
            return gdp->status;
        case 0x1: return gdp->cr1;
        case 0x2: return gdp->cr2;
        case 0x3: return gdp->ch_size;
        case 0x5: return gdp->dx;
        case 0x7: return gdp->dy;
        case 0x8: return (uint8_t)(gdp->x >> 8);
        case 0x9: return (uint8_t)(gdp->x & 0xFF);
        case 0xA: return (uint8_t)(gdp->y >> 8);
        case 0xB: return (uint8_t)(gdp->y & 0xFF);
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
                gdp->busy_ticks = 2;
            }
            break;
        case 0x1: gdp->cr1 = data; break;
        case 0x2: gdp->cr2 = data; break;
        case 0x3: gdp->ch_size = data; break;
        case 0x5: gdp->dx = data; break;
        case 0x7: gdp->dy = data; break;
        case 0x8: gdp->x = (uint16_t)((gdp->x & 0x00FFu) | ((uint16_t)data << 8)); break;
        case 0x9: gdp->x = (uint16_t)((gdp->x & 0xFF00u) | data); break;
        case 0xA: gdp->y = (uint16_t)((gdp->y & 0x00FFu) | ((uint16_t)data << 8)); break;
        case 0xB: gdp->y = (uint16_t)((gdp->y & 0xFF00u) | data); break;
        default: break;
    }
    gdp->ready = true;
    gdp->status = _ef9367_status(gdp);
}

uint64_t ef9367_tick(ef9367_t *gdp, uint64_t pins) {
    CHIPS_ASSERT(gdp);

    if ((pins & EF9367_RESET) == 0) {
        ef9367_reset(gdp);
        return pins;
    }

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

    /* active-low CS/RD/WR */
    if ((pins & EF9367_CS) == 0) {
        const uint8_t idx = EF9367_GET_ADDR(pins);
        if ((pins & EF9367_RD) == 0) {
            EF9367_SET_DATA(pins, _ef9367_read_idx(gdp, idx));
        } else if ((pins & EF9367_WR) == 0) {
            _ef9367_write_idx(gdp, idx, EF9367_GET_DATA(pins));
        }
    }

    if (gdp->busy_ticks) {
        gdp->busy_ticks--;
    }
    gdp->scan_ctr++;
    if (gdp->scan_ctr >= 1024) {
        gdp->scan_ctr = 0;
    }
    gdp->vblank = (gdp->scan_ctr >= 992);
    gdp->ready = true;
    gdp->status = _ef9367_status(gdp);
    if (gdp->vblank) {
        pins |= EF9367_VBLANK;
    } else {
        pins &= ~EF9367_VBLANK;
    }
    return pins;
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

    /* Small vectors (bit7=1): packed dX/dY in 2-bit fields. */
    if ((cmd & 0x80) != 0) {
        const int delta_x = (int)((cmd >> 5) & 0x03);
        const int delta_y = (int)((cmd >> 3) & 0x03);
        _ef9367_draw_vector(gdp, cmd, delta_x, delta_y);
    }
    /* Standard vectors (0x10..0x17): deltas from DX/DY registers. */
    else if ((cmd & 0xF8) == 0x10) {
        _ef9367_draw_vector(gdp, cmd, (int)gdp->dx, (int)gdp->dy);
    }
    else switch (cmd) {
        case 0x04: /* clear current page */
            _ef9367_clear(gdp);
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
        case 0x05: /* Partner-observed X home */
            gdp->x = 0;
            break;
        case 0x06: /* CLS + X=Y=0 */
            _ef9367_clear(gdp);
            gdp->x = 0;
            gdp->y = 0;
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
            break;
        case 0x0A: /* EF9367 8x8 block draw */
            _ef9367_draw_block(gdp, 8, 8);
            break;
        case 0x0B: /* 4x4 block */
            _ef9367_draw_block(gdp, 4, 4);
            break;
        case 0x0D: /* X register reset to 0 */
            gdp->x = 0;
            break;
        case 0x0E: /* Y register reset to 0 */
            gdp->y = 0;
            break;
        default:
            break;
    }
    gdp->busy_ticks = 2;
    gdp->ready = true;
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
