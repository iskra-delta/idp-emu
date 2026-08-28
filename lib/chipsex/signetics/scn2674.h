#pragma once
/*#
    # scn2674.h

    Header-only Signetics SCN2674 AVDC emulator in CHIPS pin style.

    Emulated bus view (for current GDP bring-up):
    - 8-bit data bus D0..D7
    - 4-bit register select A0..A3 (maps to ports 0x34..0x3F)
    - CS/RD/WR control
    - RESET, IRQ, blanking and horizontal/vertical-sync outputs

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
    Timing is expressed in AVDC character clocks (CCLK).  The optional clock
    configuration models the SCB2675 dot-clock divider when the host tick is a
    CPU/master clock rather than CCLK itself.
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
#define SCN2674_PIN_VBLANK      (29)
#define SCN2674_PIN_HSYNC       (30)
#define SCN2674_PIN_CHAR_LOAD   (31)
#define SCN2674_PIN_ATTR_LOAD   (32)
#define SCN2674_PIN_CHAR_OE     (33)
#define SCN2674_PIN_ATTR_OE     (34)
#define SCN2674_PIN_BLANK       (35)
#define SCN2674_PIN_VSYNC       (36)
#define SCN2674_PIN_CURSOR      (37)
#define SCN2674_PIN_LINE_ZERO   (38)
#define SCN2674_PIN_ODD         (39)
#define SCN2674_PIN_LAST_LINE   (40)

#define SCN2674_RD              (1ULL << SCN2674_PIN_RD)
#define SCN2674_WR              (1ULL << SCN2674_PIN_WR)
#define SCN2674_CS              (1ULL << SCN2674_PIN_CS)
#define SCN2674_RESET           (1ULL << SCN2674_PIN_RESET)
#define SCN2674_IRQ             (1ULL << SCN2674_PIN_IRQ)
#define SCN2674_VBLANK          (1ULL << SCN2674_PIN_VBLANK)
#define SCN2674_HSYNC           (1ULL << SCN2674_PIN_HSYNC)
#define SCN2674_CHAR_LOAD       (1ULL << SCN2674_PIN_CHAR_LOAD)
#define SCN2674_ATTR_LOAD       (1ULL << SCN2674_PIN_ATTR_LOAD)
#define SCN2674_CHAR_OE         (1ULL << SCN2674_PIN_CHAR_OE)
#define SCN2674_ATTR_OE         (1ULL << SCN2674_PIN_ATTR_OE)
#define SCN2674_BLANK           (1ULL << SCN2674_PIN_BLANK)
#define SCN2674_VSYNC           (1ULL << SCN2674_PIN_VSYNC)
#define SCN2674_CURSOR          (1ULL << SCN2674_PIN_CURSOR)
#define SCN2674_LINE_ZERO       (1ULL << SCN2674_PIN_LINE_ZERO)
#define SCN2674_ODD             (1ULL << SCN2674_PIN_ODD)
#define SCN2674_LAST_LINE       (1ULL << SCN2674_PIN_LAST_LINE)

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
    uint16_t row_start_addr;
    uint16_t memory_addr;
    uint16_t next_row_addr;
    bool start1_reload_pending;
    uint8_t rows_per_screen;
    uint16_t chars_per_row;
    uint8_t scanlines_per_char_row;
    uint8_t scanlines_per_field_row;
    bool double_height_width_enabled;
    bool composite_sync_enabled;
    uint8_t buffer_mode;
    bool interlace_enabled;
    bool interlace_sync_and_video;
    uint16_t equalizing_constant;
    uint8_t hsync_width;
    uint8_t hback_porch;
    uint16_t hfront_porch;
    uint8_t vfront_porch;
    uint8_t vsync_width;
    uint8_t vback_porch;
    bool character_blink_slow;
    uint8_t cursor_first_line;
    uint8_t cursor_last_line;
    bool cursor_blink_enabled;
    bool cursor_blink_slow;
    uint8_t underline_line;
    uint16_t display_buffer_last_addr;
    uint8_t double_mode[2];
    bool use_row_table;
    bool row_table_pending;
    bool row_table_pending_value;
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
    uint32_t busy_ticks;
    uint8_t delayed_cmd;
    uint8_t delayed_phase;
    bool delayed_started;
    uint32_t blink_ticks;
    uint32_t field_count;
    bool character_blink_on;
    bool cursor_blink_on;
    bool odd_field;
    uint32_t master_clock_hz;
    uint32_t dot_clock_hz;
    uint32_t dot_clock_accum;
    uint8_t dots_per_character;
    uint8_t cclk_dot_phase;
    uint16_t raster_tick_div;
    uint16_t raster_char;
    uint16_t raster_line;
    uint8_t current_line_address;
    bool raster_started;
    uint8_t display_enable_pending;
    bool display_address_floating;
    bool gfx_pending;
    bool gfx_pending_value;
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
uint64_t scn2674_tick_idle(scn2674_t *avdc, uint64_t pins,
                           uint64_t previous_pins);
uint64_t scn2674_sample_pins(scn2674_t *avdc, uint64_t pins);
void scn2674_set_clock(scn2674_t *avdc, uint32_t master_hz,
                       uint32_t dot_hz, uint8_t dots_per_character);
void scn2674_set_interlace_video(scn2674_t *avdc, bool enabled);
uint16_t scn2674_display_address(const scn2674_t *avdc, uint32_t offset);

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

static inline uint16_t _scn2674_total_chars(const scn2674_t *avdc) {
    /* EC = total/2 - 2*HSYNC, hence total = 2*(EC + 2*HSYNC). */
    uint32_t total = 2u * ((uint32_t)avdc->equalizing_constant +
                           2u * (uint32_t)avdc->hsync_width);
    if (total == 0u) total = 1u;
    if (total > 0xFFFFu) total = 0xFFFFu;
    return (uint16_t)total;
}

static inline uint16_t _scn2674_visible_lines(const scn2674_t *avdc) {
    uint32_t lines = (uint32_t)avdc->rows_per_screen *
                     (uint32_t)avdc->scanlines_per_field_row;
    if (lines == 0u) lines = 1u;
    if (lines > 0xFFFFu) lines = 0xFFFFu;
    return (uint16_t)lines;
}

static inline uint16_t _scn2674_total_lines(const scn2674_t *avdc) {
    uint32_t lines = (uint32_t)_scn2674_visible_lines(avdc) +
                     (uint32_t)avdc->vfront_porch +
                     (uint32_t)avdc->vsync_width +
                     (uint32_t)avdc->vback_porch;
    /* RS-170 interlace gives the odd field an extra two half-lines: one in
       each vertical porch.  This also shifts its VSYNC edges by half a line. */
    if (avdc->interlace_enabled && avdc->odd_field) lines++;
    if (lines == 0u) lines = 1u;
    if (lines > 0xFFFFu) lines = 0xFFFFu;
    return (uint16_t)lines;
}

static inline bool _scn2674_hblank(const scn2674_t *avdc) {
    return avdc->raster_char >= avdc->chars_per_row;
}

static inline bool _scn2674_vblank(const scn2674_t *avdc) {
    return avdc->raster_line >= _scn2674_visible_lines(avdc);
}

static inline bool _scn2674_hsync(const scn2674_t *avdc) {
    const uint32_t start = (uint32_t)avdc->chars_per_row +
                           (uint32_t)avdc->hfront_porch;
    return ((uint32_t)avdc->raster_char >= start) &&
           ((uint32_t)avdc->raster_char < start + avdc->hsync_width);
}

static inline bool _scn2674_vsync(const scn2674_t *avdc) {
    const uint32_t htotal = _scn2674_total_chars(avdc);
    uint32_t start = ((uint32_t)_scn2674_visible_lines(avdc) +
                      (uint32_t)avdc->vfront_porch) * htotal;
    const uint32_t pos = (uint32_t)avdc->raster_line * htotal +
                         avdc->raster_char;
    if (avdc->interlace_enabled && avdc->odd_field) start += htotal / 2u;
    return pos >= start && pos < start + (uint32_t)avdc->vsync_width * htotal;
}

static inline bool _scn2674_csync(const scn2674_t *avdc) {
    const uint32_t htotal = _scn2674_total_chars(avdc);
    const uint32_t half = htotal >= 2u ? htotal / 2u : 1u;
    const uint32_t hsync_start =
        ((uint32_t)avdc->chars_per_row + avdc->hfront_porch) % htotal;
    const uint32_t pos = (uint32_t)avdc->raster_line * htotal +
                         avdc->raster_char;
    uint32_t vertical_start =
        ((uint32_t)_scn2674_visible_lines(avdc) + avdc->vfront_porch) * htotal;
    uint32_t vertical_end;
    uint32_t pre_equalize;
    uint32_t post_equalize;
    uint32_t phase;
    uint32_t narrow;
    if (avdc->interlace_enabled && avdc->odd_field)
        vertical_start += half;
    vertical_end = vertical_start + (uint32_t)avdc->vsync_width * htotal;
    pre_equalize = vertical_start > 3u * htotal
        ? vertical_start - 3u * htotal : 0u;
    post_equalize = vertical_end + 3u * htotal;
    phase = (pos + htotal - hsync_start) % half;
    narrow = avdc->hsync_width >= 2u ? avdc->hsync_width / 2u : 1u;

    if ((pos >= pre_equalize && pos < vertical_start) ||
        (pos >= vertical_end && pos < post_equalize))
        return phase < narrow;             /* equalizing pulse */
    if (pos >= vertical_start && pos < vertical_end)
        return phase >= narrow;            /* serrated broad sync */
    return _scn2674_hsync(avdc);
}

typedef struct {
    uint16_t row;
    uint8_t scan;
    bool row_start;
    bool top_partial;
    bool bottom_partial;
} _scn2674_active_pos_t;

static inline _scn2674_active_pos_t
_scn2674_active_position(const scn2674_t *avdc, uint16_t line) {
    _scn2674_active_pos_t pos;
    const uint16_t lines = avdc->scanlines_per_field_row
        ? avdc->scanlines_per_field_row : 1u;
    pos.row = (uint16_t)(line / lines);
    pos.scan = (uint8_t)(line % lines);
    pos.row_start = pos.scan == 0u;
    pos.top_partial = false;
    pos.bottom_partial = false;

    if (avdc->scroll_start && avdc->scroll_end &&
        avdc->split_register[0] <= avdc->split_register[1]) {
        const uint32_t first = (uint32_t)avdc->split_register[0] * lines;
        const uint32_t after = ((uint32_t)avdc->split_register[1] + 1u) * lines;
        if ((uint32_t)line >= first && (uint32_t)line < after) {
            const uint16_t scroll = avdc->scroll_lines < lines
                ? avdc->scroll_lines : (uint16_t)(lines - 1u);
            const uint32_t shifted = (uint32_t)line - first + scroll;
            pos.row = (uint16_t)(avdc->split_register[0] + shifted / lines);
            pos.scan = (uint8_t)(shifted % lines);
            pos.row_start = ((uint32_t)line == first) || pos.scan == 0u;
            pos.top_partial = pos.row == avdc->split_register[0];
            pos.bottom_partial = pos.row > avdc->split_register[1];
        }
    }
    return pos;
}

static inline bool _scn2674_last_line_mux(const scn2674_t *avdc) {
    uint16_t next_line;
    _scn2674_active_pos_t next;
    if (!_scn2674_hblank(avdc) || _scn2674_vblank(avdc)) return false;
    next_line = (uint16_t)(avdc->raster_line + 1u);
    if (next_line >= _scn2674_visible_lines(avdc)) return false;
    next = _scn2674_active_position(avdc, next_line);
    return next.scan + 1u == avdc->scanlines_per_field_row;
}

static inline uint16_t _scn2674_next_display_addr(const scn2674_t *avdc,
                                                   uint16_t addr) {
    const uint16_t a = (uint16_t)(addr & 0x3FFFu);
    if (a == (avdc->display_buffer_last_addr & 0x3FFFu))
        return (uint16_t)(avdc->display_buffer_first_addr & 0x3FFFu);
    return (uint16_t)((a + 1u) & 0x3FFFu);
}

static inline void _scn2674_decode_ir(scn2674_t *avdc) {
    static const uint8_t vsw[4] = { 3u, 1u, 5u, 7u };
    const uint8_t encoded_lines = (uint8_t)((avdc->ir[0] >> 3) & 0x0Fu);
    const uint8_t base_lines = (uint8_t)(encoded_lines + 1u);

    avdc->double_height_width_enabled = (avdc->ir[0] & 0x80u) != 0u;
    avdc->composite_sync_enabled = (avdc->ir[0] & 0x04u) != 0u;
    avdc->buffer_mode = (uint8_t)(avdc->ir[0] & 0x03u);
    avdc->interlace_enabled = (avdc->ir[1] & 0x80u) != 0u;
    avdc->scanlines_per_char_row = avdc->interlace_enabled
        ? (uint8_t)(base_lines * 2u) : base_lines;
    avdc->scanlines_per_field_row = base_lines;
    avdc->equalizing_constant = (uint16_t)((avdc->ir[1] & 0x7Fu) + 1u);
    avdc->hsync_width = (uint8_t)(2u * (((avdc->ir[2] >> 3) & 0x0Fu) + 1u));
    avdc->hback_porch = (avdc->ir[2] & 0x07u)
        ? (uint8_t)(4u * (avdc->ir[2] & 0x07u) - 1u) : 0u;
    avdc->vfront_porch = (uint8_t)(4u * (((avdc->ir[3] >> 5) & 0x07u) + 1u));
    avdc->vback_porch = (uint8_t)(2u * ((avdc->ir[3] & 0x1Fu) + 2u));
    avdc->character_blink_slow = (avdc->ir[4] & 0x80u) != 0u;
    avdc->rows_per_screen = (uint8_t)((avdc->ir[4] & 0x7Fu) + 1u);
    avdc->chars_per_row = (uint16_t)avdc->ir[5] + 1u;
    avdc->cursor_first_line = (uint8_t)((avdc->ir[6] >> 4) & 0x0Fu);
    avdc->cursor_last_line = (uint8_t)(avdc->ir[6] & 0x0Fu);
    avdc->vsync_width = vsw[(avdc->ir[7] >> 6) & 0x03u];
    avdc->cursor_blink_enabled = (avdc->ir[7] & 0x20u) != 0u;
    avdc->cursor_blink_slow = (avdc->ir[7] & 0x10u) != 0u;
    avdc->underline_line = (uint8_t)(avdc->ir[7] & 0x0Fu);
    avdc->display_buffer_first_addr =
        (uint16_t)((((uint16_t)avdc->ir[9] & 0x0Fu) << 8) | avdc->ir[8]);
    avdc->display_buffer_last_nibble = (uint8_t)(avdc->ir[9] >> 4);
    avdc->display_buffer_last_addr =
        (uint16_t)((((uint16_t)avdc->display_buffer_last_nibble + 1u) * 1024u) - 1u);
    avdc->display_ptr_addr =
        (uint16_t)((((uint16_t)avdc->ir[11] & 0x3Fu) << 8) | avdc->ir[10]);
    avdc->reset_scanline_counter_on_scrollup = (avdc->ir[11] & 0x40u) != 0u;
    avdc->reset_scanline_counter_on_scrolldown = (avdc->ir[11] & 0x80u) != 0u;
    avdc->scroll_start = (avdc->ir[12] & 0x80u) != 0u;
    avdc->split_register[0] = (uint8_t)(avdc->ir[12] & 0x7Fu);
    avdc->scroll_end = (avdc->ir[13] & 0x80u) != 0u;
    avdc->split_register[1] = (uint8_t)(avdc->ir[13] & 0x7Fu);
    avdc->double_mode[0] = (uint8_t)((avdc->ir[14] >> 6) & 0x03u);
    avdc->double_mode[1] = (uint8_t)((avdc->ir[14] >> 4) & 0x03u);
    /* IR14[3:0] is the actual scan-line offset (0..15), not an encoded
       count-minus-one field. */
    avdc->scroll_lines = (uint8_t)(avdc->ir[14] & 0x0Fu);

    {
        const uint16_t total = _scn2674_total_chars(avdc);
        const uint32_t used = (uint32_t)avdc->chars_per_row +
                              (uint32_t)avdc->hsync_width +
                              (uint32_t)avdc->hback_porch;
        avdc->hfront_porch = (used < total) ? (uint16_t)(total - used) : 0u;
        if (avdc->raster_char >= total) avdc->raster_char = 0u;
    }
    {
        const uint16_t total = _scn2674_total_lines(avdc);
        if (avdc->raster_line >= total) avdc->raster_line = 0u;
    }
}

static inline uint8_t _scn2674_read_idx(scn2674_t *avdc, uint8_t idx) {
    switch (idx & 0x0Fu) {
        case 0x0: return (uint8_t)(avdc->irq_status & 0x1Fu);
        case 0x1: return (uint8_t)((avdc->status_latch & 0x1Fu) |
                                   (avdc->delayed_cmd == 0u ? 0x20u : 0u));
        case 0x2: return (uint8_t)(avdc->start1_addr & 0xFFu);
        case 0x3: return (uint8_t)((avdc->start1_addr >> 8) & 0x3Fu);
        case 0x4: return (uint8_t)(avdc->cursor_addr & 0xFFu);
        case 0x5: return (uint8_t)((avdc->cursor_addr >> 8) & 0x3Fu);
        case 0x6: return (uint8_t)(avdc->start2_addr & 0xFFu);
        case 0x7: return (uint8_t)((avdc->start2_addr >> 8) & 0x3Fu);
        default: return 0xFFu;
    }
}

static inline void _scn2674_write_cell(scn2674_t *avdc, uint16_t addr) {
    const uint16_t a = (uint16_t)(addr & 0x3FFFu);
    avdc->vram[a] = avdc->char_latch;
    avdc->attr_vram[a] = avdc->attr_latch;
}

static inline void _scn2674_read_cell(scn2674_t *avdc, uint16_t addr) {
    const uint16_t a = (uint16_t)(addr & 0x3FFFu);
    avdc->char_latch = avdc->vram[a];
    avdc->attr_latch = avdc->attr_vram[a];
}

static inline void _scn2674_event(scn2674_t *avdc, uint8_t bit) {
    bit &= 0x1Fu;
    avdc->status_latch |= bit;
    if (avdc->irq_mask & bit) avdc->irq_status |= bit;
}

static inline void _scn2674_finish_delayed(scn2674_t *avdc) {
    avdc->delayed_cmd = 0u;
    avdc->busy_ticks = 0u;
    avdc->delayed_phase = 0u;
    avdc->delayed_started = false;
    _scn2674_event(avdc, 0x02u);
}

static inline void _scn2674_execute_single(scn2674_t *avdc, uint8_t cmd) {
    switch (cmd) {
        case 0xA2: _scn2674_write_cell(avdc, avdc->display_ptr_addr); break;
        case 0xA4: _scn2674_read_cell(avdc, avdc->display_ptr_addr); break;
        case 0xA9:
            avdc->cursor_addr = (uint16_t)((avdc->cursor_addr + 1u) & 0x3FFFu);
            break;
        case 0xAA: _scn2674_write_cell(avdc, avdc->cursor_addr); break;
        case 0xAB:
            _scn2674_write_cell(avdc, avdc->cursor_addr);
            avdc->cursor_addr = (uint16_t)((avdc->cursor_addr + 1u) & 0x3FFFu);
            break;
        case 0xAC: _scn2674_read_cell(avdc, avdc->cursor_addr); break;
        case 0xAD:
            _scn2674_read_cell(avdc, avdc->cursor_addr);
            avdc->cursor_addr = (uint16_t)((avdc->cursor_addr + 1u) & 0x3FFFu);
            break;
        default: break;
    }
}

static inline bool _scn2674_command_is_delayed(uint8_t cmd) {
    return cmd == 0xA2u || cmd == 0xA4u || cmd == 0xA9u ||
           cmd == 0xAAu || cmd == 0xABu || cmd == 0xACu ||
           cmd == 0xADu || cmd == 0xBBu || cmd == 0xBDu;
}

static inline void _scn2674_start_delayed(scn2674_t *avdc, uint8_t cmd) {
    uint32_t work;
    if (avdc->delayed_cmd != 0u) return;
    avdc->delayed_cmd = cmd;
    avdc->delayed_phase = 0u;
    avdc->delayed_started = (cmd == 0xA9u);
    if (cmd == 0xA9u) {
        work = 3u;
    } else if (cmd == 0xBBu || cmd == 0xBDu) {
        const uint32_t cells =
            ((uint32_t)avdc->display_ptr_addr - (uint32_t)avdc->cursor_addr) & 0x3FFFu;
        work = (cells + 1u) * 2u;
    } else {
        work = 5u;
    }
    avdc->busy_ticks = work;
}

static inline void _scn2674_master_reset(scn2674_t *avdc) {
    avdc->cmd = 0u;
    avdc->ir_ptr = 0u;
    avdc->status_latch = 0u;
    avdc->irq_status = 0u;
    avdc->irq_mask = 0u;
    avdc->irq_live = 0u;
    avdc->status = 0x20u;
    avdc->delayed_cmd = 0u;
    avdc->busy_ticks = 0u;
    avdc->delayed_phase = 0u;
    avdc->delayed_started = false;
    avdc->display_enabled = false;
    avdc->display_enable_pending = 0u;
    avdc->display_address_floating = false;
    avdc->cursor_enabled = false;
    avdc->gfx_enabled = false;
    avdc->gfx_pending = false;
    avdc->ir[2] &= 0x7Fu;
    avdc->use_row_table = false;
    avdc->row_table_pending = false;
    avdc->raster_char = 0u;
    avdc->raster_tick_div = 0u;
    avdc->raster_line = 0u;
    avdc->raster_started = false;
    avdc->odd_field = false;
    avdc->row_start_addr = avdc->start1_addr;
    avdc->memory_addr = avdc->start1_addr;
    avdc->next_row_addr = avdc->start1_addr;
    avdc->start1_reload_pending = false;
    _scn2674_decode_ir(avdc);
}

static inline void _scn2674_exec_cmd(scn2674_t *avdc, uint8_t cmd) {
    avdc->cmd = cmd;
    if (cmd == 0x00u) {
        _scn2674_master_reset(avdc);
        return;
    }
    if ((cmd & 0xF0u) == 0x10u) {
        const uint8_t v = (uint8_t)(cmd & 0x0Fu);
        if (v <= 14u) avdc->ir_ptr = v;
        return;
    }
    if ((cmd & 0xE0u) == 0x20u) {
        const bool on = (cmd & 0x01u) != 0u;
        /* The three controls can be combined; d bits truly are don't-care. */
        if (cmd & 0x02u) {
            avdc->gfx_pending = true;
            avdc->gfx_pending_value = on;
        }
        if (cmd & 0x08u) {
            if (!on) {
                avdc->display_enabled = false;
                avdc->display_enable_pending = 0u;
                avdc->display_address_floating = (cmd & 0x04u) != 0u;
            } else {
                avdc->display_address_floating = false;
                avdc->display_enable_pending = (cmd & 0x04u) ? 2u : 1u;
            }
        }
        if (cmd & 0x10u) avdc->cursor_enabled = on;
        return;
    }
    switch (cmd & 0xE0u) {
        case 0x40u:
            avdc->status_latch &= (uint8_t)~(cmd & 0x1Fu);
            avdc->irq_status &= (uint8_t)~(cmd & 0x1Fu);
            return;
        case 0x60u:
            /* 011nnnnn: enable the selected interrupt sources. */
            avdc->irq_mask |= (uint8_t)(cmd & 0x1Fu);
            return;
        case 0x80u:
            /* 100nnnnn: disable the selected interrupt sources. */
            avdc->irq_mask &= (uint8_t)~(cmd & 0x1Fu);
            return;
        default:
            break;
    }
    if (_scn2674_command_is_delayed(cmd)) _scn2674_start_delayed(avdc, cmd);
}

static inline void _scn2674_write_ir(scn2674_t *avdc, uint8_t data) {
    const uint8_t index = avdc->ir_ptr <= 14u ? avdc->ir_ptr : 14u;
    const bool old_row_table = (avdc->ir[2] & 0x80u) != 0u;
    avdc->ir[index] = data;
    _scn2674_decode_ir(avdc);
    if (index == 2u) {
        const bool requested = (data & 0x80u) != 0u;
        avdc->row_table_pending = (requested != old_row_table) ||
                                  (requested != avdc->use_row_table);
        avdc->row_table_pending_value = requested;
    }
    if (avdc->ir_ptr < 14u) avdc->ir_ptr++;
}

static inline void _scn2674_write_idx(scn2674_t *avdc, uint8_t idx, uint8_t data) {
    switch (idx & 0x0Fu) {
        case 0x0: _scn2674_write_ir(avdc, data); break;
        case 0x1: _scn2674_exec_cmd(avdc, data); break;
        case 0x2:
            avdc->start1_addr = (uint16_t)((avdc->start1_addr & 0x3F00u) | data);
            if (avdc->raster_started && avdc->display_enabled &&
                !_scn2674_vblank(avdc)) {
                avdc->start1_reload_pending = true;
            } else {
                avdc->row_start_addr = avdc->start1_addr;
                avdc->memory_addr = avdc->start1_addr;
                avdc->next_row_addr = avdc->start1_addr;
            }
            break;
        case 0x3:
            avdc->start1_upper_raw = data;
            avdc->start1_addr = (uint16_t)((avdc->start1_addr & 0x00FFu) |
                                           ((uint16_t)(data & 0x3Fu) << 8));
            if (avdc->double_height_width_enabled) {
                avdc->ir[14] = (uint8_t)((avdc->ir[14] & 0x3Fu) | (data & 0xC0u));
                _scn2674_decode_ir(avdc);
            }
            if (avdc->raster_started && avdc->display_enabled &&
                !_scn2674_vblank(avdc)) {
                avdc->start1_reload_pending = true;
            } else {
                avdc->row_start_addr = avdc->start1_addr;
                avdc->memory_addr = avdc->start1_addr;
                avdc->next_row_addr = avdc->start1_addr;
            }
            break;
        case 0x4:
            avdc->cursor_addr = (uint16_t)((avdc->cursor_addr & 0x3F00u) | data);
            avdc->addr_latch = avdc->cursor_addr;
            avdc->addr_latch_dirty = false;
            break;
        case 0x5:
            avdc->cursor_addr = (uint16_t)((avdc->cursor_addr & 0x00FFu) |
                                           ((uint16_t)(data & 0x3Fu) << 8));
            avdc->addr_latch = avdc->cursor_addr;
            avdc->addr_latch_dirty = false;
            break;
        case 0x6:
            avdc->start2_addr = (uint16_t)((avdc->start2_addr & 0x3F00u) | data);
            avdc->start2_addr_start =
                (uint16_t)((avdc->start2_addr_start & 0x3F00u) | data);
            break;
        case 0x7:
            avdc->start2_addr = (uint16_t)((avdc->start2_addr & 0x00FFu) |
                                           ((uint16_t)(data & 0x3Fu) << 8));
            avdc->start2_addr_start =
                (uint16_t)((avdc->start2_addr_start & 0x00FFu) |
                           ((uint16_t)(data & 0x3Fu) << 8));
            avdc->split_use_screen2[0] = (data & 0x40u) != 0u;
            avdc->split_use_screen2[1] = (data & 0x80u) != 0u;
            break;
        default: break;
    }
}

void scn2674_init(scn2674_t *avdc) {
    CHIPS_ASSERT(avdc);
    memset(avdc, 0, sizeof(*avdc));
    scn2674_reset(avdc);
}

void scn2674_reset(scn2674_t *avdc) {
    bool glyph_rom_loaded;
    CHIPS_ASSERT(avdc);
    glyph_rom_loaded = avdc->glyph_rom_loaded;
    memset(avdc->ir, 0, sizeof(avdc->ir));
    memset(avdc->vram, 0x20, sizeof(avdc->vram));
    memset(avdc->attr_vram, 0x00, sizeof(avdc->attr_vram));
    avdc->char_latch = 0x20u;
    avdc->attr_latch = 0u;
    avdc->addr_latch = 0u;
    avdc->addr_latch_dirty = false;
    avdc->cursor_addr = 0u;
    avdc->display_ptr_addr = 0u;
    avdc->display_buffer_first_addr = 0u;
    avdc->display_buffer_last_nibble = 0u;
    avdc->display_buffer_last_addr = 1023u;
    avdc->start1_addr = 0u;
    avdc->start1_upper_raw = 0u;
    avdc->start2_addr = 0u;
    avdc->start2_addr_start = 0u;
    avdc->row_start_addr = 0u;
    avdc->memory_addr = 0u;
    avdc->next_row_addr = 0u;
    avdc->start1_reload_pending = false;
    avdc->split_use_screen2[0] = false;
    avdc->split_use_screen2[1] = false;
    avdc->master_clock_hz = 1u;
    avdc->dot_clock_hz = 1u;
    avdc->dot_clock_accum = 0u;
    avdc->dots_per_character = 1u;
    avdc->cclk_dot_phase = 0u;
    avdc->field_count = 0u;
    avdc->blink_ticks = 0u;
    avdc->character_blink_on = true;
    avdc->cursor_blink_on = true;
    avdc->interlace_sync_and_video = false;
    _scn2674_decode_ir(avdc);
    _scn2674_master_reset(avdc);
    /* The character generator is external ROM and is unaffected by an AVDC
       reset or master-reset command. */
    avdc->glyph_rom_loaded = glyph_rom_loaded;
}

static inline uint32_t _scn2674_blank_clocks_remaining(const scn2674_t *avdc) {
    const uint16_t htotal = _scn2674_total_chars(avdc);
    if (!avdc->display_enabled) return 0xFFFFFFFFu;
    if (_scn2674_vblank(avdc)) {
        const uint16_t vtotal = _scn2674_total_lines(avdc);
        return (uint32_t)(htotal - avdc->raster_char) +
               (uint32_t)(vtotal - avdc->raster_line - 1u) * htotal;
    }
    if (_scn2674_hblank(avdc)) return (uint32_t)(htotal - avdc->raster_char);
    return 0u;
}

static inline bool _scn2674_row_table_fetch_cycle(const scn2674_t *avdc) {
    uint16_t next_line;
    _scn2674_active_pos_t next;
    if (!avdc->display_enabled || !avdc->use_row_table ||
        _scn2674_vblank(avdc) || !_scn2674_hblank(avdc) ||
        avdc->raster_char < avdc->chars_per_row ||
        avdc->raster_char >= avdc->chars_per_row + 2u)
        return false;
    next_line = (uint16_t)(avdc->raster_line + 1u);
    if (next_line >= _scn2674_visible_lines(avdc)) return false;
    next = _scn2674_active_position(avdc, next_line);
    return next.row_start;
}

static inline void _scn2674_process_delayed(scn2674_t *avdc) {
    const uint8_t cmd = avdc->delayed_cmd;
    if (cmd == 0u) return;

    /* The two CCLK row-table fetch owns the display bus at the start of the
       blank preceding a new character row.  Delayed memory commands pause
       for those clocks exactly as shown in the AVDC bus timing diagrams. */
    if (_scn2674_row_table_fetch_cycle(avdc)) return;

    if (cmd == 0xBBu || cmd == 0xBDu) {
        const bool blank = !avdc->display_enabled || _scn2674_hblank(avdc) ||
                           _scn2674_vblank(avdc);
        if (!blank) return;
        if (avdc->busy_ticks > 0u) avdc->busy_ticks--;
        avdc->delayed_phase ^= 1u;
        if (avdc->delayed_phase == 0u) {
            if (cmd == 0xBBu) _scn2674_write_cell(avdc, avdc->cursor_addr);
            else _scn2674_read_cell(avdc, avdc->cursor_addr);
            if (avdc->cursor_addr == avdc->display_ptr_addr) {
                _scn2674_finish_delayed(avdc);
            } else {
                avdc->cursor_addr = (uint16_t)((avdc->cursor_addr + 1u) & 0x3FFFu);
            }
        }
        return;
    }

    if (cmd != 0xA9u && avdc->delayed_started && avdc->display_enabled &&
        !_scn2674_hblank(avdc) && !_scn2674_vblank(avdc))
        return;

    if (!avdc->delayed_started) {
        if (_scn2674_blank_clocks_remaining(avdc) < avdc->busy_ticks) return;
        avdc->delayed_started = true;
    }
    if (avdc->busy_ticks > 0u) avdc->busy_ticks--;
    if (avdc->busy_ticks == 0u) {
        _scn2674_execute_single(avdc, cmd);
        _scn2674_finish_delayed(avdc);
    }
}

static inline void _scn2674_update_blink(scn2674_t *avdc) {
    const uint32_t char_period = avdc->character_blink_slow ? 128u : 64u;
    const uint32_t cursor_period = avdc->cursor_blink_slow ? 64u : 32u;
    avdc->character_blink_on =
        (avdc->field_count % char_period) < (char_period / 2u);
    avdc->cursor_blink_on = !avdc->cursor_blink_enabled ||
        ((avdc->field_count % cursor_period) < (cursor_period / 2u));
    avdc->blink_ticks = avdc->field_count;
}

static inline void _scn2674_fetch_row_table(scn2674_t *avdc) {
    const uint16_t p = (uint16_t)(avdc->start2_addr & 0x3FFFu);
    const uint8_t lo = avdc->vram[p];
    const uint8_t hi = avdc->vram[(p + 1u) & 0x3FFFu];
    avdc->start1_upper_raw = hi;
    avdc->start1_addr = (uint16_t)((((uint16_t)hi & 0x3Fu) << 8) | lo);
    if (avdc->double_height_width_enabled) {
        avdc->ir[14] = (uint8_t)((avdc->ir[14] & 0x3Fu) | (hi & 0xC0u));
        _scn2674_decode_ir(avdc);
    }
    avdc->start2_addr = (uint16_t)((p + 2u) & 0x3FFFu);
}

static inline void _scn2674_begin_scanline(scn2674_t *avdc) {
    const uint16_t visible = _scn2674_visible_lines(avdc);
    _scn2674_active_pos_t pos;
    bool split1;
    bool split2;
    bool automatic_split;
    if (avdc->display_enable_pending == 1u) {
        avdc->display_enabled = true;
        avdc->display_enable_pending = 0u;
    }
    if (avdc->raster_line == visible) _scn2674_event(avdc, 0x10u);
    if (avdc->raster_line >= visible) return;

    pos = _scn2674_active_position(avdc, avdc->raster_line);
    split1 = avdc->raster_line ==
        (uint32_t)avdc->split_register[0] * avdc->scanlines_per_field_row;
    split2 = avdc->raster_line ==
        (uint32_t)(avdc->split_register[1] +
                   ((avdc->scroll_start && avdc->scroll_end) ? 1u : 0u)) *
        avdc->scanlines_per_field_row;

    if (pos.scan == 0u) _scn2674_event(avdc, 0x08u);
    if (split1) _scn2674_event(avdc, 0x04u);
    if (split2) _scn2674_event(avdc, 0x01u);

    if (!pos.row_start) {
        if (!avdc->gfx_enabled) avdc->memory_addr = avdc->row_start_addr;
        return;
    }

    if (avdc->row_table_pending) {
        avdc->use_row_table = avdc->row_table_pending_value;
        avdc->row_table_pending = false;
    }
    if (avdc->gfx_pending) {
        avdc->gfx_enabled = avdc->gfx_pending_value;
        avdc->gfx_pending = false;
    }

    automatic_split =
        (avdc->split_use_screen2[0] && split1) ||
        (avdc->split_use_screen2[1] &&
         avdc->raster_line ==
            (uint32_t)(avdc->split_register[1] + 1u) *
            avdc->scanlines_per_field_row);

    if (avdc->use_row_table && avdc->display_enabled) {
        _scn2674_fetch_row_table(avdc);
        avdc->row_start_addr = avdc->start1_addr;
        avdc->start1_reload_pending = false;
    } else if (avdc->raster_line == 0u) {
        avdc->row_start_addr = avdc->start1_addr;
        avdc->start1_reload_pending = false;
    } else if (automatic_split) {
        avdc->row_start_addr = avdc->start2_addr;
    } else if (avdc->start1_reload_pending) {
        avdc->row_start_addr = avdc->start1_addr;
        avdc->start1_reload_pending = false;
    } else {
        avdc->row_start_addr = avdc->next_row_addr;
    }
    avdc->memory_addr = avdc->row_start_addr;
}

static inline void _scn2674_advance_cclk(scn2674_t *avdc) {
    const uint16_t htotal = _scn2674_total_chars(avdc);
    if (!avdc->raster_started) {
        avdc->raster_started = true;
        _scn2674_begin_scanline(avdc);
    }
    _scn2674_process_delayed(avdc);
    if (!_scn2674_vblank(avdc) && avdc->raster_char < avdc->chars_per_row) {
        avdc->memory_addr = _scn2674_next_display_addr(avdc, avdc->memory_addr);
        if (avdc->raster_char + 1u == avdc->chars_per_row)
            avdc->next_row_addr = avdc->memory_addr;
    }
    avdc->raster_char++;
    avdc->raster_tick_div = avdc->raster_char;
    if (avdc->raster_char < htotal) return;

    avdc->raster_char = 0u;
    avdc->raster_tick_div = 0u;
    avdc->raster_line++;
    if (avdc->raster_line >= _scn2674_total_lines(avdc)) {
        avdc->raster_line = 0u;
        if (avdc->interlace_enabled) avdc->odd_field = !avdc->odd_field;
        else avdc->odd_field = false;
        avdc->field_count++;
        _scn2674_update_blink(avdc);
        if (avdc->display_enable_pending == 2u) {
            avdc->display_enabled = true;
            avdc->display_enable_pending = 0u;
        }
    }
    _scn2674_begin_scanline(avdc);
}

static inline bool _scn2674_advance_master_tick(scn2674_t *avdc) {
    uint64_t accum;
    uint32_t dot_edges;
    uint32_t cclk_edges;
    uint32_t phase;
    if (avdc->master_clock_hz == 0u || avdc->dot_clock_hz == 0u ||
        avdc->dots_per_character == 0u) return false;
    accum = (uint64_t)avdc->dot_clock_accum + avdc->dot_clock_hz;
    dot_edges = (uint32_t)(accum / avdc->master_clock_hz);
    avdc->dot_clock_accum = (uint32_t)(accum % avdc->master_clock_hz);
    /* Dot edges have no observable intermediate state: only a completed
       character clock advances the raster. Fold all edges from this master
       tick into one quotient/remainder operation instead of looping over the
       4..6 dot edges generated on every Partner GDP CPU clock. */
    phase = (uint32_t)avdc->cclk_dot_phase + dot_edges;
    cclk_edges = phase / avdc->dots_per_character;
    avdc->cclk_dot_phase = (uint8_t)(phase % avdc->dots_per_character);
    const bool advanced = cclk_edges != 0u;
    while (cclk_edges-- > 0u)
        _scn2674_advance_cclk(avdc);
    return advanced;
}

uint16_t scn2674_display_address(const scn2674_t *avdc, uint32_t offset) {
    uint32_t start;
    uint32_t last;
    uint32_t first;
    uint32_t until_wrap;
    uint32_t range;
    CHIPS_ASSERT(avdc);
    start = avdc->start1_addr & 0x3FFFu;
    first = avdc->display_buffer_first_addr & 0x3FFFu;
    last = avdc->display_buffer_last_addr & 0x3FFFu;
    if (first > last) return (uint16_t)((start + offset) & 0x3FFFu);
    until_wrap = (start <= last) ? (last - start + 1u)
                                 : (0x4000u - start + last + 1u);
    if (offset < until_wrap) return (uint16_t)((start + offset) & 0x3FFFu);
    range = last - first + 1u;
    return (uint16_t)(first + ((offset - until_wrap) % range));
}

void scn2674_set_clock(scn2674_t *avdc, uint32_t master_hz,
                       uint32_t dot_hz, uint8_t dots_per_character) {
    CHIPS_ASSERT(avdc);
    avdc->master_clock_hz = master_hz ? master_hz : 1u;
    avdc->dot_clock_hz = dot_hz;
    avdc->dots_per_character = dots_per_character ? dots_per_character : 1u;
    if (avdc->dot_clock_accum >= avdc->master_clock_hz)
        avdc->dot_clock_accum %= avdc->master_clock_hz;
    if (avdc->cclk_dot_phase >= avdc->dots_per_character)
        avdc->cclk_dot_phase %= avdc->dots_per_character;
}

void scn2674_set_interlace_video(scn2674_t *avdc, bool enabled) {
    CHIPS_ASSERT(avdc);
    avdc->interlace_sync_and_video = enabled;
}

uint64_t scn2674_sample_pins(scn2674_t *avdc, uint64_t pins) {
    uint64_t outputs = SCN2674_IRQ | SCN2674_VBLANK | SCN2674_HSYNC |
                       SCN2674_BLANK | SCN2674_VSYNC | SCN2674_CURSOR |
                       SCN2674_LINE_ZERO | SCN2674_ODD | SCN2674_LAST_LINE;
    const bool hblank = _scn2674_hblank(avdc);
    const bool vblank = _scn2674_vblank(avdc);
    const bool hsync = _scn2674_hsync(avdc);
    const bool vsync = _scn2674_vsync(avdc);
    const _scn2674_active_pos_t pos =
        _scn2674_active_position(avdc, avdc->raster_line);
    const bool line_zero = avdc->raster_char == 0u && !vblank &&
                           pos.scan == 0u;
    bool cursor = false;
    pins &= ~outputs;
    avdc->status = (uint8_t)((avdc->delayed_cmd == 0u ? 0x20u : 0u) |
                             (avdc->status_latch & 0x1Fu));
    avdc->irq_live = (uint8_t)((avdc->delayed_cmd == 0u ? 0x02u : 0u) |
                               (vblank ? 0x10u : 0u) |
                               (line_zero ? 0x08u : 0u));
    if (avdc->irq_status & 0x1Fu) pins |= SCN2674_IRQ;
    if (vblank) pins |= SCN2674_VBLANK;
    if (hsync) pins |= SCN2674_HSYNC;
    if (!avdc->display_enabled || hblank || vblank) pins |= SCN2674_BLANK;
    if (avdc->composite_sync_enabled ? _scn2674_csync(avdc) : vsync)
        pins |= SCN2674_VSYNC;
    if (line_zero) pins |= SCN2674_LINE_ZERO;
    if (avdc->odd_field) pins |= SCN2674_ODD;
    /* DADD13 is multiplexed with LL during horizontal blank.  Partner
       captures this value at BLANK's trailing edge to inhibit CPU AVDC
       accesses throughout the last scan line of every character row. */
    if (_scn2674_last_line_mux(avdc)) pins |= SCN2674_LAST_LINE;

    {
        uint8_t scan = pos.scan;
        if ((pos.top_partial && avdc->reset_scanline_counter_on_scrolldown) ||
            (pos.bottom_partial && avdc->reset_scanline_counter_on_scrollup))
            scan = 0u;
        avdc->current_line_address =
            (avdc->interlace_enabled && avdc->interlace_sync_and_video)
                ? (uint8_t)(scan * 2u + (avdc->odd_field ? 1u : 0u))
                : scan;
    }

    if (avdc->display_enabled && avdc->cursor_enabled && avdc->cursor_blink_on &&
        !hblank && !vblank) {
        cursor = avdc->memory_addr == avdc->cursor_addr &&
                 avdc->current_line_address >= avdc->cursor_first_line &&
                 avdc->current_line_address <= avdc->cursor_last_line;
    }
    if (cursor) pins |= SCN2674_CURSOR;
    return pins;
}

uint64_t scn2674_tick(scn2674_t *avdc, uint64_t pins) {
    CHIPS_ASSERT(avdc);
    if ((pins & SCN2674_RESET) == 0u) {
        scn2674_reset(avdc);
        return scn2674_sample_pins(avdc, pins);
    }
    if (pins & SCN2674_CHAR_LOAD) avdc->char_latch = SCN2674_GET_DATA(pins);
    if (pins & SCN2674_ATTR_LOAD) avdc->attr_latch = SCN2674_GET_DATA(pins);
    if (pins & SCN2674_CHAR_OE) {
        SCN2674_SET_DATA(pins, avdc->char_latch);
    } else if (pins & SCN2674_ATTR_OE) {
        SCN2674_SET_DATA(pins, avdc->attr_latch);
    }

    if ((pins & SCN2674_CS) == 0u) {
        const uint8_t idx = SCN2674_GET_ADDR(pins);
        if ((pins & SCN2674_RD) == 0u) {
            SCN2674_SET_DATA(pins, _scn2674_read_idx(avdc, idx));
        } else if ((pins & SCN2674_WR) == 0u) {
            _scn2674_write_idx(avdc, idx, SCN2674_GET_DATA(pins));
        }
    }
    _scn2674_advance_master_tick(avdc);
    return scn2674_sample_pins(avdc, pins);
}

uint64_t scn2674_tick_idle(scn2674_t *avdc, uint64_t pins,
                           uint64_t previous_pins) {
    static const uint64_t outputs = SCN2674_IRQ | SCN2674_VBLANK |
        SCN2674_HSYNC | SCN2674_BLANK | SCN2674_VSYNC | SCN2674_CURSOR |
        SCN2674_LINE_ZERO | SCN2674_ODD | SCN2674_LAST_LINE;
    if ((pins & SCN2674_RESET) == 0u || (pins & SCN2674_CS) == 0u ||
        (pins & (SCN2674_CHAR_LOAD | SCN2674_ATTR_LOAD |
                 SCN2674_CHAR_OE | SCN2674_ATTR_OE)) != 0u)
        return scn2674_tick(avdc, pins);

    /* Output and status pins change only on a completed character clock.
       The 18/24 MHz dot clock often leaves the previous CCLK state intact on
       a 4 MHz motherboard tick, so reuse its already-sampled output pins. */
    const bool irq_changed = ((avdc->irq_status & 0x1Fu) != 0u) !=
                             ((previous_pins & SCN2674_IRQ) != 0u);
    if (_scn2674_advance_master_tick(avdc) || irq_changed)
        return scn2674_sample_pins(avdc, pins);
    return (pins & ~outputs) | (previous_pins & outputs);
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
