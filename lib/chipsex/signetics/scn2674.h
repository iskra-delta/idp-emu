#pragma once
/*#
    # scn2674.h

    Header-only minimal Signetics SCN2674 AVDC emulator in CHIPS pin style.

    Emulated bus view (for current GDP bring-up):
    - 8-bit data bus D0..D7
    - 4-bit register select A0..A3 (maps to ports 0x34..0x3F)
    - CS/RD/WR control
    - RESET and IRQ placeholder

    ## Emulated Pins

    ***************************************
    *           +-----------+             *
    * D0..D7 <->|           |<-> A0..A3   *
    *           | SCN2674   |             *
    *   CS̅  --->|   AVDC    |---> IRQ     *
    *   RD̅  --->|           |             *
    *   WR̅  --->|           |             *
    * RESET̅ --->|           |             *
    *           +-----------+             *
    ***************************************

    - D0..D7: bidirectional data bus
    - A0..A3: register index (0x34..0x3F window)
    - CS̅/RD̅/WR̅: active-low bus control
    - RESET̅: active-low reset
    - IRQ: interrupt output placeholder (for future full AVDC interrupt model)
#*/
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* bus helpers: address in bits 0..3, data in bits 16..23 */
#define SCN2674_GET_ADDR(p)     ((uint8_t)((p) & 0x0FULL))
#define SCN2674_GET_DATA(p)     ((uint8_t)(((p) >> 16) & 0xFF))
#define SCN2674_SET_DATA(p, d)  { p = ((p) & ~0xFF0000ULL) | (((uint64_t)(d) << 16) & 0xFF0000ULL); }

/* control pins */
#define SCN2674_PIN_RD          (24)
#define SCN2674_PIN_WR          (25)
#define SCN2674_PIN_CS          (26)
#define SCN2674_PIN_RESET       (27)
#define SCN2674_PIN_IRQ         (28)

#define SCN2674_RD              (1ULL << SCN2674_PIN_RD)
#define SCN2674_WR              (1ULL << SCN2674_PIN_WR)
#define SCN2674_CS              (1ULL << SCN2674_PIN_CS)
#define SCN2674_RESET           (1ULL << SCN2674_PIN_RESET)
#define SCN2674_IRQ             (1ULL << SCN2674_PIN_IRQ)

typedef struct {
    uint8_t ir[16];
    uint8_t ir_ptr;
    uint8_t status;
    uint8_t status_latch;
    uint8_t irq_status;
    uint8_t irq_mask;
    uint8_t cmd;
    bool display_enabled;
    bool gfx_enabled;
    uint8_t char_latch;
    uint8_t attr_latch;
    uint16_t addr_latch;
    bool addr_latch_dirty;
    uint16_t cursor_addr;
    uint16_t display_ptr_addr;
    uint16_t display_buffer_first_addr;
    uint8_t display_buffer_last_nibble;
    uint16_t start1_addr;
    uint8_t start1_upper_raw;
    uint16_t start2_addr;
    uint16_t start2_addr_start;
    uint8_t rows_per_screen;
    uint8_t chars_per_row;
    uint8_t scanlines_per_char_row;
    bool use_row_table;
    bool cursor_enabled;
    bool split_use_screen2[2];
    bool reset_scanline_counter_on_scrollup;
    bool reset_scanline_counter_on_scrolldown;
    bool scroll_start;
    bool scroll_end;
    uint8_t split_register[2];
    uint8_t scroll_lines;
    uint8_t vram[16384];
    uint8_t attr_vram[16384];
    uint16_t busy_ticks;
    uint32_t blink_ticks;
    uint16_t raster_tick_div;
    uint16_t raster_line;
    uint8_t irq_live;
    bool prev_ready_flag;
    bool prev_split1_flag;
    bool prev_split2_flag;
    bool prev_line_zero_flag;
    bool prev_vblank_flag;
    uint8_t glyph_rom[256][11];
    bool glyph_rom_loaded;
} scn2674_t;

void scn2674_init(scn2674_t *avdc);
void scn2674_reset(scn2674_t *avdc);
uint64_t scn2674_tick(scn2674_t *avdc, uint64_t pins);

/* convenience wrappers used by board-level code */
uint8_t scn2674_read(scn2674_t *avdc, uint8_t port);
void scn2674_write(scn2674_t *avdc, uint8_t port, uint8_t data);
bool scn2674_load_charset_rom(scn2674_t *avdc, const uint8_t *rom, uint32_t size);

#ifdef __cplusplus
}
#endif

#ifdef CHIPS_IMPL
#ifndef CHIPS_ASSERT
    #include <assert.h>
    #define CHIPS_ASSERT(c) assert(c)
#endif

static inline uint8_t _scn2674_read_idx(scn2674_t *avdc, uint8_t idx) {
    switch (idx & 0x0F) {
        case 0x0: return (uint8_t)(avdc->irq_status & 0x1Fu);
        // Status register: latched interrupt events | live READY bit.
        // status_latch accumulates events regardless of irq_mask.
        case 0x1: return (uint8_t)((avdc->status_latch & 0x1Fu) |
                                   (avdc->busy_ticks == 0u ? 0x20u : 0x00u));
        case 0x2: return (uint8_t)(avdc->start1_addr & 0xFF);
        case 0x3: return (uint8_t)(avdc->start1_addr >> 8);
        case 0x4: return (uint8_t)(avdc->cursor_addr & 0xFF);
        case 0x5: return (uint8_t)(avdc->cursor_addr >> 8);
        case 0x6: return (uint8_t)(avdc->start2_addr & 0xFF);
        case 0x7: return (uint8_t)(avdc->start2_addr >> 8);
        default: return 0xFF;
    }
}

static inline void _scn2674_write_cell(scn2674_t *avdc, uint16_t addr) {
    const uint16_t a = (uint16_t)(addr & 0x3FFFu);
    avdc->vram[a] = avdc->char_latch;
    avdc->attr_vram[a] = avdc->attr_latch;
}

static inline void _scn2674_write_byte(scn2674_t *avdc, uint16_t addr) {
    const uint16_t a = (uint16_t)(addr & 0x3FFFu);
    avdc->vram[a] = avdc->char_latch;
    avdc->attr_vram[a] = avdc->attr_latch;
}

static inline void _scn2674_read_byte(scn2674_t *avdc, uint16_t addr) {
    const uint16_t a = (uint16_t)(addr & 0x3FFFu);
    avdc->char_latch = avdc->vram[a];
    avdc->attr_latch = avdc->attr_vram[a];
}

static inline void _scn2674_read_cell(scn2674_t *avdc, uint16_t addr) {
    const uint16_t a = (uint16_t)(addr & 0x3FFFu);
    avdc->char_latch = avdc->vram[a];
    avdc->attr_latch = avdc->attr_vram[a];
}

static inline void _scn2674_fill_cursor_to_ptr(scn2674_t *avdc) {
    uint16_t a = (uint16_t)(avdc->cursor_addr & 0x3FFFu);
    const uint16_t b = (uint16_t)(avdc->display_ptr_addr & 0x3FFFu);
    if (a > b) {
        return;
    }
    while (a <= b) {
        _scn2674_write_cell(avdc, a);
        a = (uint16_t)(a + 1u);
    }
}

static inline void _scn2674_exec_cmd(scn2674_t *avdc, uint8_t cmd) {
    avdc->cmd = cmd;
    switch (cmd & 0xF0) {
        case 0x10:
            avdc->ir_ptr = (uint8_t)(cmd & 0x0F);
            break;
        case 0x00:
            /* Master reset command. */
            avdc->ir_ptr = 0;
            avdc->status_latch = 0x00;
            avdc->irq_status = 0x00;
            avdc->irq_mask = 0x00;
            avdc->status = 0x20;
            avdc->irq_live = 0x00;
            avdc->raster_tick_div = 0;
            avdc->raster_line = 0;
            avdc->start1_upper_raw = 0;
            avdc->prev_ready_flag = true;
            avdc->prev_split1_flag = false;
            avdc->prev_split2_flag = false;
            avdc->prev_line_zero_flag = true;
            avdc->prev_vblank_flag = false;
            avdc->gfx_enabled = false;
            avdc->display_enabled = false;
            avdc->cursor_enabled = false;
            avdc->use_row_table = false;
            break;
        case 0x20:
        case 0x30:
            /* Display/cursor/gfx enable-disable group (0x20-0x3F).
               Bit2=gfx select, Bit3=display select, Bit4=cursor select, Bit0=enable. */
            if (cmd & 0x02) {
                avdc->gfx_enabled = (cmd & 0x01) != 0;
            }
            if (cmd & 0x08) {
                avdc->display_enabled = (cmd & 0x01) != 0;
            }
            if (cmd & 0x10) {
                avdc->cursor_enabled = (cmd & 0x01) != 0;
            }
            break;
        case 0x40:
        case 0x50:
            // Reset (clear) latched status/irq bits — covers both 0x4X and 0x5X encodings.
            avdc->status_latch &= (uint8_t)~(cmd & 0x1F);
            avdc->irq_status &= (uint8_t)~(cmd & 0x1F);
            break;
        case 0x70:
            avdc->irq_mask &= (uint8_t)~(cmd & 0x1F);
            break;
        case 0x90:
            avdc->irq_mask |= (uint8_t)(cmd & 0x1F);
            break;
        case 0xA0:
        case 0xB0:
            switch (cmd) {
                case 0xA2: /* write at display pointer */
                    _scn2674_write_byte(avdc, avdc->display_ptr_addr);
                    break;
                case 0xA4: /* read at display pointer */
                    _scn2674_read_byte(avdc, avdc->display_ptr_addr);
                    break;
                case 0xA9: /* increment cursor */
                    avdc->cursor_addr = (uint16_t)((avdc->cursor_addr + 1u) & 0x3FFFu);
                    break;
                case 0xAA: /* write at cursor + increment (GDP runtime path) */
                    _scn2674_write_byte(avdc, avdc->cursor_addr);
                    avdc->cursor_addr = (uint16_t)((avdc->cursor_addr + 1u) & 0x3FFFu);
                    break;
                case 0xAB: /* write at cursor + increment (observed GDP runtime text path) */
                    _scn2674_write_byte(avdc, avdc->cursor_addr);
                    avdc->cursor_addr = (uint16_t)((avdc->cursor_addr + 1u) & 0x3FFFu);
                    break;
                case 0xAF: /* write at cursor + increment */
                    _scn2674_write_byte(avdc, avdc->cursor_addr);
                    avdc->cursor_addr = (uint16_t)((avdc->cursor_addr + 1u) & 0x3FFFu);
                    break;
                case 0xAC: /* read at cursor */
                    _scn2674_read_byte(avdc, avdc->cursor_addr);
                    break;
                case 0xAD: /* read at cursor + increment */
                    _scn2674_read_byte(avdc, avdc->cursor_addr);
                    avdc->cursor_addr = (uint16_t)((avdc->cursor_addr + 1u) & 0x3FFFu);
                    break;
                case 0xBB: /* write cursor..pointer */
                    _scn2674_fill_cursor_to_ptr(avdc);
                    avdc->cursor_addr = avdc->display_ptr_addr;
                    avdc->busy_ticks = 8;
                    break;
                case 0xBF:
                    _scn2674_fill_cursor_to_ptr(avdc);
                    avdc->cursor_addr = avdc->display_ptr_addr;
                    avdc->busy_ticks = 8;
                    break;
                default:
                    break;
            }
            break;
        default:
            break;
    }
}

static inline void _scn2674_write_idx(scn2674_t *avdc, uint8_t idx, uint8_t data) {
    switch (idx & 0x0F) {
        case 0x0:
            avdc->ir[avdc->ir_ptr & 0x0F] = data;
            switch (avdc->ir_ptr & 0x0F) {
                case 0x0:
                    avdc->scanlines_per_char_row = (uint8_t)(((data & 0x78u) >> 3) + 1u);
                    if ((data & 0x80u) != 0u) {
                        avdc->ir[14] = (uint8_t)((avdc->ir[14] & 0x3Fu) | (avdc->start1_upper_raw & 0xC0u));
                    }
                    break;
                case 0x2:
                    avdc->use_row_table = (data & 0x80) != 0;
                    break;
                case 0x4:
                    avdc->rows_per_screen = (uint8_t)((data & 0x7F) + 1u);
                    break;
                case 0x5:
                    avdc->chars_per_row = (uint8_t)(data + 1);
                    break;
                case 0x8:
                    avdc->display_buffer_first_addr =
                        (uint16_t)((avdc->display_buffer_first_addr & 0x0F00u) | data);
                    break;
                case 0x9:
                    avdc->display_buffer_first_addr =
                        (uint16_t)((avdc->display_buffer_first_addr & 0x00FFu) |
                                   (((uint16_t)(data & 0x0Fu)) << 8));
                    avdc->display_buffer_last_nibble = (uint8_t)((data >> 4) & 0x0Fu);
                    break;
                case 0xA:
                    avdc->display_ptr_addr = (uint16_t)((avdc->display_ptr_addr & 0x3F00u) | data);
                    break;
                case 0xB:
                    avdc->display_ptr_addr = (uint16_t)((avdc->display_ptr_addr & 0x00FFu) | ((uint16_t)(data & 0x3F) << 8));
                    avdc->reset_scanline_counter_on_scrollup = (data & 0x40u) != 0;
                    avdc->reset_scanline_counter_on_scrolldown = (data & 0x80u) != 0;
                    break;
                case 0xC:
                    avdc->scroll_start = (data & 0x80u) != 0;
                    avdc->split_register[0] = (uint8_t)(data & 0x7Fu);
                    break;
                case 0xD:
                    avdc->scroll_end = (data & 0x80u) != 0;
                    avdc->split_register[1] = (uint8_t)(data & 0x7Fu);
                    break;
                case 0xE:
                    avdc->scroll_lines = (uint8_t)(data & 0x0Fu);
                    break;
                default:
                    break;
            }
            if (avdc->ir_ptr < 14) avdc->ir_ptr++;
            break;
        case 0x1:
            _scn2674_exec_cmd(avdc, data);
            break;
        case 0x2:
            avdc->start1_addr = (uint16_t)((avdc->start1_addr & 0x3F00u) | data);
            break;
        case 0x3:
            avdc->start1_upper_raw = data;
            avdc->start1_addr = (uint16_t)((avdc->start1_addr & 0x00FFu) | ((uint16_t)(data & 0x3F) << 8));
            if ((avdc->ir[0] & 0x80u) != 0u) {
                avdc->ir[14] = (uint8_t)((avdc->ir[14] & 0x3Fu) | (data & 0xC0u));
            }
            break;
        case 0x4:
            avdc->cursor_addr = (uint16_t)((avdc->cursor_addr & 0x3F00u) | data);
            avdc->addr_latch = avdc->cursor_addr;
            avdc->addr_latch_dirty = false;
            break;
        case 0x5:
            avdc->cursor_addr = (uint16_t)((avdc->cursor_addr & 0x00FFu) | ((uint16_t)(data & 0x3F) << 8));
            avdc->addr_latch = avdc->cursor_addr;
            avdc->addr_latch_dirty = false;
            break;
        case 0x6:
            avdc->start2_addr = (uint16_t)((avdc->start2_addr & 0x3F00u) | data);
            avdc->start2_addr_start = (uint16_t)((avdc->start2_addr_start & 0x3F00u) | data);
            break;
        case 0x7:
            avdc->start2_addr = (uint16_t)((avdc->start2_addr & 0x00FFu) | ((uint16_t)(data & 0x3F) << 8));
            avdc->start2_addr_start = (uint16_t)((avdc->start2_addr_start & 0x00FFu) | ((uint16_t)(data & 0x3F) << 8));
            avdc->split_use_screen2[0] = (data & 0x40u) != 0;
            avdc->split_use_screen2[1] = (data & 0x80u) != 0;
            avdc->start2_addr &= 0x3FFFu;
            avdc->start2_addr_start &= 0x3FFFu;
            break;
        default:
            break;
    }
}

void scn2674_init(scn2674_t *avdc) {
    CHIPS_ASSERT(avdc);
    memset(avdc, 0, sizeof(*avdc));
    scn2674_reset(avdc);
}

void scn2674_reset(scn2674_t *avdc) {
    CHIPS_ASSERT(avdc);
    memset(avdc->ir, 0, sizeof(avdc->ir));
    memset(avdc->vram, 0x20, sizeof(avdc->vram));
    memset(avdc->attr_vram, 0x00, sizeof(avdc->attr_vram));
    avdc->ir_ptr = 0;
    avdc->status = 0x20;
    avdc->status_latch = 0x00;
    avdc->irq_status = 0x00;
    avdc->irq_mask = 0x00;
    avdc->cmd = 0;
    avdc->display_enabled = false;
    avdc->gfx_enabled = false;
    avdc->char_latch = 0x20;
    avdc->attr_latch = 0x00;
    avdc->addr_latch = 0;
    avdc->addr_latch_dirty = false;
    avdc->cursor_addr = 0;
    avdc->display_ptr_addr = 0;
    avdc->display_buffer_first_addr = 0;
    avdc->display_buffer_last_nibble = 0;
    avdc->start1_addr = 0;
    avdc->start1_upper_raw = 0;
    avdc->start2_addr = 0;
    avdc->start2_addr_start = 0;
    avdc->rows_per_screen = 25;
    avdc->chars_per_row = 80;
    avdc->scanlines_per_char_row = 12;
    avdc->use_row_table = false;
    avdc->cursor_enabled = false;
    avdc->split_use_screen2[0] = false;
    avdc->split_use_screen2[1] = false;
    avdc->reset_scanline_counter_on_scrollup = false;
    avdc->reset_scanline_counter_on_scrolldown = false;
    avdc->scroll_start = false;
    avdc->scroll_end = false;
    avdc->split_register[0] = 0;
    avdc->split_register[1] = 0;
    avdc->scroll_lines = 0;
    avdc->busy_ticks = 0;
    avdc->blink_ticks = 0;
    avdc->raster_tick_div = 0;
    avdc->raster_line = 0;
    avdc->irq_live = 0;
    avdc->prev_ready_flag = true;
    avdc->prev_split1_flag = false;
    avdc->prev_split2_flag = false;
    avdc->prev_line_zero_flag = true;
    avdc->prev_vblank_flag = false;
    avdc->glyph_rom_loaded = false;
}

uint64_t scn2674_tick(scn2674_t *avdc, uint64_t pins) {
    CHIPS_ASSERT(avdc);

    if ((pins & SCN2674_RESET) == 0) {
        scn2674_reset(avdc);
        return pins;
    }

    /* active-low CS/RD/WR */
    if ((pins & SCN2674_CS) == 0) {
        const uint8_t idx = SCN2674_GET_ADDR(pins);
        if ((pins & SCN2674_RD) == 0) {
            SCN2674_SET_DATA(pins, _scn2674_read_idx(avdc, idx));
        } else if ((pins & SCN2674_WR) == 0) {
            _scn2674_write_idx(avdc, idx, SCN2674_GET_DATA(pins));
        }
    }

    if (avdc->busy_ticks) {
        avdc->busy_ticks--;
    }
    const bool ready_flag = (avdc->busy_ticks == 0);
    if (ready_flag && !avdc->prev_ready_flag) {
        avdc->status_latch |= 0x02u;
        if (avdc->irq_mask & 0x02u) {
            avdc->irq_status |= 0x02u;
        }
    }
    avdc->prev_ready_flag = ready_flag;

    /* Build a simple raster phase model to drive live and latched status bits:
       bit0=split2, bit1=ready, bit2=split1, bit3=line0, bit4=vblank. */
    const uint16_t scanlines = (avdc->scanlines_per_char_row == 0u)
        ? 1u : (uint16_t)avdc->scanlines_per_char_row;
    const uint16_t rows = (avdc->rows_per_screen == 0u)
        ? 1u : (uint16_t)avdc->rows_per_screen;
    const uint16_t visible_lines = (uint16_t)(rows * scanlines);
    uint16_t vblank_lines = (uint16_t)(scanlines * 2u);
    if (vblank_lines < 8u) {
        vblank_lines = 8u;
    }
    const uint16_t total_lines = (uint16_t)(visible_lines + vblank_lines);
    const uint16_t ticks_per_scanline = 128u;
    avdc->raster_tick_div++;
    if (avdc->raster_tick_div >= ticks_per_scanline) {
        avdc->raster_tick_div = 0;
        avdc->raster_line = (uint16_t)(avdc->raster_line + 1u);
        if (avdc->raster_line >= total_lines) {
            avdc->raster_line = 0;
        }
    }
    if (avdc->raster_line >= total_lines) {
        avdc->raster_line = 0;
    }

    const uint8_t char_row = (uint8_t)((avdc->raster_line / scanlines) & 0x7Fu);
    const bool line_zero_live = (avdc->raster_line == 0u);
    const bool vblank_live = (avdc->raster_line >= visible_lines);
    const bool split1_live =
        avdc->scroll_start && (char_row == (uint8_t)(avdc->split_register[0] & 0x7Fu));
    const bool split2_live =
        avdc->scroll_end && (char_row == (uint8_t)(avdc->split_register[1] & 0x7Fu));
    avdc->irq_live =
        (uint8_t)((split2_live ? 0x01u : 0u) |
                  (ready_flag ? 0x02u : 0u) |
                  (split1_live ? 0x04u : 0u) |
                  (line_zero_live ? 0x08u : 0u) |
                  (vblank_live ? 0x10u : 0u));

    if (split2_live && !avdc->prev_split2_flag) {
        avdc->status_latch |= 0x01u;
        if (avdc->irq_mask & 0x01u) {
            avdc->irq_status |= 0x01u;
        }
    }
    if (split1_live && !avdc->prev_split1_flag) {
        avdc->status_latch |= 0x04u;
        if (avdc->irq_mask & 0x04u) {
            avdc->irq_status |= 0x04u;
        }
    }
    if (line_zero_live && !avdc->prev_line_zero_flag) {
        avdc->status_latch |= 0x08u;
        if (avdc->irq_mask & 0x08u) {
            avdc->irq_status |= 0x08u;
        }
    }
    if (vblank_live && !avdc->prev_vblank_flag) {
        avdc->status_latch |= 0x10u;
        if (avdc->irq_mask & 0x10u) {
            avdc->irq_status |= 0x10u;
        }
    }
    avdc->prev_split2_flag = split2_live;
    avdc->prev_split1_flag = split1_live;
    avdc->prev_line_zero_flag = line_zero_live;
    avdc->prev_vblank_flag = vblank_live;

    /* Status register bit5=RDFLG, low 5 bits=latched status causes regardless of masks. */
    avdc->status = (uint8_t)((ready_flag ? 0x20u : 0x00u) | (avdc->status_latch & 0x1Fu));
    avdc->blink_ticks++;

    if (avdc->irq_status & 0x1Fu) {
        pins |= SCN2674_IRQ;
    } else {
        pins &= ~SCN2674_IRQ;
    }
    return pins;
}

uint8_t scn2674_read(scn2674_t *avdc, uint8_t port) {
    CHIPS_ASSERT(avdc);
    if (port < 0x34 || port > 0x3F) {
        return 0xFF;
    }
    return _scn2674_read_idx(avdc, (uint8_t)(port - 0x34));
}

void scn2674_write(scn2674_t *avdc, uint8_t port, uint8_t data) {
    CHIPS_ASSERT(avdc);
    if (port < 0x34 || port > 0x3F) {
        return;
    }
    _scn2674_write_idx(avdc, (uint8_t)(port - 0x34), data);
}

bool scn2674_load_charset_rom(scn2674_t *avdc, const uint8_t *rom, uint32_t size) {
    CHIPS_ASSERT(avdc);
    if (!rom || size < 2048) {
        return false;
    }
    /* Accept both 2KB (8x8) and 2816-byte (8x11) character-generator ROMs. */
    const int rows = (size >= 2816) ? 11 : 8;
    memset(avdc->glyph_rom, 0, sizeof(avdc->glyph_rom));
    for (int ch = 0; ch < 256; ch++) {
        for (int row = 0; row < rows; row++) {
            avdc->glyph_rom[ch][row] = rom[(ch * rows) + row];
        }
    }
    avdc->glyph_rom_loaded = true;
    return true;
}
#endif
