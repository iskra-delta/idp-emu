#include "partner_gdp.hpp"
#include "gui/display.hpp"
#include "ef9367_font.hpp"
#include "scn2674_font.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {
constexpr uint8_t EF9367_CMD_PORT = 0x20;
constexpr uint8_t EF9367_CR1_PORT = 0x21;
constexpr uint8_t EF9367_CR2_PORT = 0x22;
constexpr uint8_t EF9367_CHSZ_PORT = 0x23;
constexpr uint8_t EF9367_DX_PORT = 0x25;
constexpr uint8_t EF9367_DY_PORT = 0x27;
constexpr uint8_t EF9367_XH_PORT = 0x28;
constexpr uint8_t EF9367_XL_PORT = 0x29;
constexpr uint8_t EF9367_YH_PORT = 0x2A;
constexpr uint8_t EF9367_YL_PORT = 0x2B;
constexpr uint8_t EF9367_STSNI_PORT = 0x2F;

constexpr uint8_t AVDC_BASE_PORT = 0x34;
constexpr uint8_t AVDC_LAST_PORT = 0x3F;
constexpr uint8_t AVDC_STATUS_PORT = 0x39;
constexpr uint16_t AVDC_UDG_BASE = 0x2000;

static inline uint64_t ef_bus_idle() {
    return EF9367_CS | EF9367_RD | EF9367_WR | EF9367_RESET |
           EF9367_IRQ | EF9367_MW | EF9367_VBLANK | EF9367_BLANK;
}

constexpr uint64_t EF_BOARD_INPUTS = EF9367_RBNK | EF9367_WBNK |
    EF9367_XORM | EF9367_FM0 | EF9367_FM1 | EF9367_SCRLM;

static inline uint8_t ef_bus_read(ef9367_t* chip, uint8_t port, uint64_t &chip_pins) {
    const uint8_t data = ef9367_read(chip, port);
    EF9367_SET_DATA(chip_pins, data);
    return data;
}

static inline void ef_bus_write(ef9367_t* chip, uint8_t port, uint8_t data,
                                uint64_t &chip_pins) {
    EF9367_SET_DATA(chip_pins, data);
    ef9367_write(chip, port, data);
}

static inline void ef_scroll_latch_write(ef9367_t* chip, uint8_t data,
                                         uint64_t &chip_pins) {
    EF9367_SET_DATA(chip_pins, data);
    chip->scroll_latch = data;
    chip->scroll_offset = (chip_pins & EF9367_SCRLM) ? 0 : (int8_t)data;
}

static inline uint64_t ef_board_pins_from_pio(uint8_t pa) {
    uint64_t pins = ef_bus_idle();
    if (pa & 0x01u) pins |= EF9367_RBNK;
    if (pa & 0x02u) pins |= EF9367_WBNK;
    if (pa & 0x04u) pins |= EF9367_XORM;
    if (pa & 0x08u) pins |= EF9367_FM0;
    if (pa & 0x10u) pins |= EF9367_FM1;
    if (pa & 0x80u) pins |= EF9367_SCRLM;
    return pins;
}

static inline uint64_t avdc_bus_idle() {
    return SCN2674_CS | SCN2674_RD | SCN2674_WR | SCN2674_RESET;
}

static inline uint8_t avdc_dots_per_character(uint8_t pio_b) {
    /* SCB2675B C1:C0: 00=10, 01=7, 10=8, 11=9 dots/CCLK. */
    static constexpr uint8_t divider[4] = { 10u, 7u, 8u, 9u };
    return divider[(pio_b >> 5) & 0x03u];
}

static inline uint32_t avdc_dot_clock_hz(uint8_t pio_b) {
    /* GDP schematic: CA7 selects the 18 MHz (80-column) or 24 MHz
       (132-column) DCLK path feeding the SCB2675. */
    return (pio_b & 0x80u) ? 24000000u : 18000000u;
}

static inline bool gdp_trace_enabled() {
    static const bool enabled = []{
        const char* s = std::getenv("IDP_TRACE_EF");
        return s && s[0] && s[0] != '0';
    }();
    return enabled;
}

static inline bool avdc_trace_enabled() {
    static const bool enabled = []{
        const char* s = std::getenv("IDP_TRACE_AVDC");
        return s && s[0] && s[0] != '0';
    }();
    return enabled;
}

static inline bool render_trace_enabled() {
    static const bool enabled = []{
        const char* s = std::getenv("IDP_TRACE_RENDER");
        return s && s[0] && s[0] != '0';
    }();
    return enabled;
}

static inline uint8_t avdc_map_port_index(uint8_t port) {
    switch (port) {
        case 0x38: return 0x0; // init/data register
        case 0x39: return 0x1; // command/status
        case 0x3A: return 0x2; // screen start 1 low
        case 0x3B: return 0x3; // screen start 1 high
        case 0x3C: return 0x4; // cursor low
        case 0x3D: return 0x5; // cursor high
        case 0x3E: return 0x6; // screen start 2 low
        case 0x3F: return 0x7; // screen start 2 high
        default:   return 0xFF;
    }
}

static inline uint8_t avdc_bus_read(scn2674_t* chip, uint8_t port,
                                    uint64_t &chip_pins) {
    const uint8_t idx = avdc_map_port_index(port);
    if (idx == 0xFF) return 0xFF;
    const uint8_t data = scn2674_read(chip, (uint8_t)(0x34u + idx));
    chip_pins = scn2674_sample_pins(chip, avdc_bus_idle());
    SCN2674_SET_DATA(chip_pins, data);
    return data;
}

static inline void avdc_bus_write(scn2674_t* chip, uint8_t port, uint8_t data,
                                  uint64_t &chip_pins) {
    const uint8_t idx = avdc_map_port_index(port);
    if (idx == 0xFF) return;
    scn2674_write(chip, (uint8_t)(0x34u + idx), data);
    chip_pins = scn2674_sample_pins(chip, avdc_bus_idle());
    SCN2674_SET_DATA(chip_pins, data);
}

static inline uint8_t avdc_latch_read(scn2674_t* chip, bool attribute,
                                      uint64_t &chip_pins) {
    const uint8_t data = attribute ? chip->attr_latch : chip->char_latch;
    chip_pins = scn2674_sample_pins(chip, avdc_bus_idle());
    SCN2674_SET_DATA(chip_pins, data);
    return data;
}

static inline void avdc_latch_write(scn2674_t* chip, bool attribute,
                                    uint8_t data, uint64_t &chip_pins) {
    if (attribute) chip->attr_latch = data;
    else chip->char_latch = data;
    chip_pins = scn2674_sample_pins(chip, avdc_bus_idle());
    SCN2674_SET_DATA(chip_pins, data);
}

static inline uint64_t gdp_pio_signal_pins(bool gdp_irq_active,
                                           bool avdc_irq_active) {
    uint64_t pins = 0;
    if (!gdp_irq_active) pins |= Z80PIO_ASTB;
    if (!avdc_irq_active) pins |= Z80PIO_BSTB;
    Z80PIO_SET_PA(pins, 0);
    Z80PIO_SET_PB(pins, 0);
    return pins;
}

static inline uint8_t gdp_pio_read(z80pio_t* pio, uint8_t port,
                                   bool gdp_irq_active, bool avdc_irq_active,
                                   uint64_t &chip_pins) {
    uint64_t pins = gdp_pio_signal_pins(gdp_irq_active, avdc_irq_active) |
                    Z80PIO_CE | Z80PIO_IORQ | Z80PIO_RD;
    if (port & 0x01) pins |= Z80PIO_CDSEL;
    if (port & 0x02) pins |= Z80PIO_BASEL;
    chip_pins = pins = z80pio_tick(pio, pins);
    return Z80PIO_GET_DATA(pins);
}

static inline void gdp_pio_write(z80pio_t* pio, uint8_t port, uint8_t data,
                                 bool gdp_irq_active, bool avdc_irq_active,
                                 uint64_t &chip_pins) {
    uint64_t pins = gdp_pio_signal_pins(gdp_irq_active, avdc_irq_active) |
                    Z80PIO_CE | Z80PIO_IORQ;
    if (port & 0x01) pins |= Z80PIO_CDSEL;
    if (port & 0x02) pins |= Z80PIO_BASEL;
    Z80PIO_SET_DATA(pins, data);
    chip_pins = z80pio_tick(pio, pins);
}
}

partner_gdp::partner_gdp(terminal_profile profile, const std::string &rtc_nvram_path)
    : partner(rtc_nvram_path), terminal_profile_(profile)
{
    set_sio_port_lock(sio_port_id::sio1_a, true, "Internal GDP keyboard (fixed)");
    sio_device_config squid;
    squid.kind = sio_device_kind::internal_squid;
    (void)set_sio_device_config(sio_port_id::sio1_b, squid);
    ef9367_init(&ef9367_);
    scn2674_init(&avdc_);
    z80pio_init(&gdp_video_pio_);
    (void)ef9367_load_charset_rom(
        &ef9367_,
        ef9367_builtin_font::rom,
        ef9367_builtin_font::rom_size);
    (void)scn2674_load_charset_rom(
        &avdc_,
        scn2674_builtin_font::rom,
        scn2674_builtin_font::rom_size);
    terminal_ = make_terminal_emulator(terminal_profile_);
}

void partner_gdp::reset()
{
    partner::reset();
    if (terminal_profile_ == terminal_profile::vt100_ansi) {
        // The GDP BIOS interprets ESC sequences according to the terminal
        // type stored in MM58167 NVRAM AB. A Partner-mode value treats ANSI
        // CSI digits as native GDP commands, which can draw tiny/rotated EF
        // glyphs. Match the BIOS configuration to the selected VT100 profile
        // while preserving its language nibble and checksum.
        uint8_t language = (uint8_t)(rtc.regs[0x0B] & 0x0Fu);
        // Language values 0..8 are defined. The historical package seed used
        // the invalid value B, which makes the GDP BIOS translate decimal
        // digits into its tiny alternate glyph range D8..E1. Migrate that
        // value to Yugoslav while preserving any valid user selection.
        if (language > 8u)
            language = 8u;
        rtc.regs[0x0B] = language;
        stamp_rtc_nvram_checksum();
    }
    raw_serial_.clear();
    ef9367_reset(&ef9367_);
    ef9367_clear_framebuffers(&ef9367_);
    scn2674_reset(&avdc_);
    z80pio_reset(&gdp_video_pio_);
    text_col_ = 0;
    text_row_ = 0;
    expect_abs_x_ = false;
    expect_abs_y_ = false;
    io_cnt_ = {};
    avdc_port_wr_cnt_.fill(0);
    avdc_cmd_cnt_.fill(0);
    avdc_char_wr_cnt_ = 0;
    avdc_char_nonspace_wr_cnt_ = 0;
    avdc_char_hist_.fill(0);
    gdp_scroll_ = 0;
    avdc_takeover_ = false;
    avdc_rowtbl_base_cache_ = 0;
    avdc_rowtbl_base_cache_valid_ = false;
    avdc_rowtbl_be_cache_ = false;
    avdc_rowtbl_bias_cache_ = 0;
    key_fifo_.clear();
    keyboard_.reset();
    avdc_pins_ = scn2674_sample_pins(&avdc_, avdc_bus_idle());
    avdc_restrict_ = false;
    uint64_t pio_idle = Z80PIO_ASTB | Z80PIO_BSTB;
    Z80PIO_SET_PA(pio_idle, 0);
    gdp_video_pio_pins_ = z80pio_tick(&gdp_video_pio_, pio_idle);
    ef9367_pins_ = ef_bus_idle();
    avdc_irq_edge_ = false;
    if (terminal_)
        terminal_->reset();
}

void partner_gdp::tick()
{
    partner::tick();

    // Keep all serial TX channels drained so ROM/CP/M polling on TX-ready can
    // progress even when the GDP runtime moves console traffic to a different
    // SIO than the early bootstrap path.
    auto drain_tx = [&](sio_port_id port, z80sio_t& chip, int channel,
                        bool keyboard_path) {
        if (!is_sio_port_locked(port) &&
            get_sio_device_config(port).kind != sio_device_kind::none)
            return;
        uint8_t data = 0;
        if (!z80sio_line_take_tx(&chip, channel, &data))
            return;
        if (keyboard_path) {
            keyboard_.host_write(data);
        }
    };
    // On Partner GDP, SIO traffic is keyboard/peripheral side traffic, not
    // video output. Drain transmit paths so firmware polling progresses, but
    // do not mirror any SIO TX bytes into the on-screen terminal/raw display
    // traces.
    drain_tx(sio_port_id::sio1_a, sio, Z80SIO_CHANNEL_A, true);
    drain_tx(sio_port_id::sio1_b, sio, Z80SIO_CHANNEL_B, false);
    drain_tx(sio_port_id::sio2_a, sio2, Z80SIO_CHANNEL_A, false);
    drain_tx(sio_port_id::sio2_b, sio2, Z80SIO_CHANNEL_B, false);

    // Feed queued keyboard input strictly through SIO RX path. Keyboard bytes
    // are hardware-serial input and must be consumed by firmware ISR/driver.
    if (!key_fifo_.empty() &&
        z80sio_rx_enabled(&sio, Z80SIO_CHANNEL_A)) {
        // On Partner GDP the keyboard is a serial device on the first SIO,
        // channel A. Keep the key path on that real channel so the SIO's RX
        // interrupt machinery remains responsible for delivering characters.
        if (z80sio_line_receive(&sio, Z80SIO_CHANNEL_A, key_fifo_.front(),
                                get_tick_count(), 4000000, 153600)) {
            key_fifo_.pop_front();
        }
    }

    const uint8_t pa = Z80PIO_GET_PA(gdp_video_pio_pins_);
    ef9367_pins_ = ef9367_tick(&ef9367_, ef_board_pins_from_pio(pa));

    const uint8_t text_ctl = Z80PIO_GET_PB(gdp_video_pio_pins_);
    scn2674_set_clock(&avdc_, 4000000u, avdc_dot_clock_hz(text_ctl),
                      avdc_dots_per_character(text_ctl));
    const bool previous_avdc_irq = (avdc_pins_ & SCN2674_IRQ) != 0;
    const bool previous_blank = (avdc_pins_ & SCN2674_BLANK) != 0;
    const bool previous_last_line = (avdc_pins_ & SCN2674_LAST_LINE) != 0;
    avdc_pins_ = scn2674_tick(&avdc_, avdc_bus_idle());
    const bool current_avdc_irq = (avdc_pins_ & SCN2674_IRQ) != 0;
    const bool current_blank = (avdc_pins_ & SCN2674_BLANK) != 0;
    /* GDP sheet 11/14: IC26 (74S374) samples multiplexed DADD13/LL on
       BLANK's trailing edge.  Its RESTRICT output is returned as port 36h
       bit 4 and stays high for the complete last scan line. */
    if (previous_blank && !current_blank)
        avdc_restrict_ = previous_last_line;
    avdc_irq_edge_ = current_avdc_irq && !previous_avdc_irq;

    const bool gdp_irq_active = (ef9367_pins_ & EF9367_IRQ) == 0;
    const bool avdc_irq_active = (avdc_pins_ & SCN2674_IRQ) != 0;
    uint64_t pio_idle = gdp_pio_signal_pins(gdp_irq_active, avdc_irq_active);
    gdp_video_pio_pins_ = z80pio_tick(&gdp_video_pio_, pio_idle);
}

uint64_t partner_gdp::clock_expansion_daisy_chain(uint64_t bus_pins)
{
    return z80pio_daisychain(&gdp_video_pio_, bus_pins);
}

void partner_gdp::render_to(display &disp)
{
    disp.set_content_origin(0, 0);
    disp.set_content_area(display::FB_W, display::FB_H);
    disp.set_preserve_aspect(true);
    disp.clear();
    int ef_on_pixels = 0;
    int avdc_pixels = 0;
    const bool serial_boot_text_available =
        (raw_serial_.find("CP/M V3.0 Loader") != std::string::npos);

    // 1) GDP boot plane (EF9367):
    // Partner GDP composes graphics in a 1056x624 visible raster where the
    // 1024x512 EF plane is centered. Map that "full raster" into our display.
    constexpr int FULL_W = 1056;
    constexpr int FULL_H = 624;
    constexpr int GFX_W = 1024;
    constexpr int GFX_H = 512;
    constexpr int GFX_OFF_X = (FULL_W - GFX_W) / 2; // 16
    constexpr int GFX_OFF_Y = (FULL_H - GFX_H) / 2; // 56

    const uint8_t text_ctl = Z80PIO_GET_PB(gdp_video_pio_pins_);
    const bool text_cursor_mode = (text_ctl & 0x02u) != 0;
    /* SCB2675 M/C is active high for monochrome operation. */
    const bool text_color_mode = (text_ctl & 0x04u) == 0;
    const bool indexed_text_output =
        text_color_mode && (disp.get_phosphor_type() == display::phosphor_type::color);
    const bool text_b3 = (text_ctl & 0x08u) != 0; // mono: ABLANK, color: blue fg
    const bool text_b4 = (text_ctl & 0x10u) != 0; // mono: light BKGND, color: green fg
    const int text_dots_per_char = (int)avdc_dots_per_character(text_ctl);
    const uint32_t text_dot_clock = avdc_dot_clock_hz(text_ctl);

    // Feed CMAC global color/mono controls into monitor shader palette.
    float fg_r = 0.92f, fg_g = 0.94f, fg_b = 0.96f;
    float bg_r = 0.00f, bg_g = 0.00f, bg_b = 0.00f;
    bool reverse_video = false;
    bool force_background = false;
    if (text_color_mode) {
        // In color mode, these lines directly select blue/green foreground channels.
        fg_r = 0.92f;
        fg_g = text_b4 ? 0.92f : 0.12f;
        fg_b = text_b3 ? 0.92f : 0.12f;
    } else {
        bg_r = bg_g = bg_b = text_b4 ? 0.18f : 0.0f;
    }
    disp.set_text_palette_rgb(
        fg_r, fg_g, fg_b,
        bg_r, bg_g, bg_b,
        reverse_video, force_background);

    const auto map_full_to_disp = [](int fx, int fy, int &dx, int &dy) {
        dx = (fx * display::FB_W) / FULL_W;
        dy = (fy * display::FB_H) / FULL_H;
    };

    const uint8_t *ef_page = ef9367_.fb[ef9367_.read_bank & 1u];
    const int scroll = ef9367_.scroll_offset;
    for (int y = 0; y < 512; y++) {
        int phys_src_y = y - scroll;
        int src_y = 0;
        bool src_visible = true;
        if (ef9367_.mode_512_lines) {
            phys_src_y &= 511;
            src_y = phys_src_y;
        } else {
            if (phys_src_y < 0 || phys_src_y >= 512) {
                src_visible = false;
            } else {
                src_y = phys_src_y >> 1;
            }
        }
        if (!src_visible) {
            continue;
        }
        for (int x = 0; x < 1024; x++) {
            const uint32_t p = (uint32_t)src_y * 1024u + (uint32_t)x;
            const uint8_t b = ef_page[p >> 3];
            if ((ef9367_.cr1 & 0x04u) == 0u &&
                (b & (uint8_t)(1u << (p & 7u)))) {
                const int fx = GFX_OFF_X + x;
                const int fy = GFX_OFF_Y + y;
                int dx = 0;
                int dy = 0;
                map_full_to_disp(fx, fy, dx, dy);
                disp.add_pixel(dx, dy, 144);
                ef_on_pixels++;
            }
        }
    }

    // 2) AVDC text plane:
    // Partner AVDC text is logically 1056x312 (132x26 chars at 8x12).
    // The monitor shows AVDC scanlines doubled vertically, so the effective
    // physical text raster becomes 1056x624 without horizontally stretching
    // the character cells.
    // IR4/IR5 determine the logical rows and columns.  CA7 does not cap the
    // character count: it changes DCLK, so a mismatched mode is physically
    // wider/narrower and is clipped by the raster just like the real board.
    const int avdc_rows = std::min(128, std::max(1, (int)avdc_.rows_per_screen));
    const int avdc_cols = std::min(256, std::max(1, (int)avdc_.chars_per_row));
    const int avdc_stride = std::max(1, (int)avdc_.chars_per_row);

    int avdc_nonspace = 0;
    for (uint8_t ch : avdc_.vram) {
        if (ch > 0x20) {
            avdc_nonspace++;
        }
    }
    int avdc_visible_nonspace = 0;

    bool use_row_table = avdc_.use_row_table;
    uint16_t rowtbl_base = (uint16_t)(avdc_.start2_addr_start & 0x3FFFu);
    bool rowtbl_big_endian = false;
    int rowtbl_le_score = 0;
    int rowtbl_be_score = 0;
    int rowtbl_stride = avdc_stride;
    int rowtbl_valid_lines = 0;
    int rowtbl_stride_hits = 0;
    const uint16_t linear_base = (uint16_t)(avdc_.start1_addr & 0x3FFFu);
    const auto wrap_display_addr = [&](uint16_t base, uint32_t delta) -> uint16_t {
        const uint32_t first = avdc_.display_buffer_first_addr & 0x3FFFu;
        const uint32_t last = avdc_.display_buffer_last_addr & 0x3FFFu;
        uint32_t start = base & 0x3FFFu;
        if (first > last) return (uint16_t)((start + delta) & 0x3FFFu);
        const uint32_t until_wrap = (start <= last)
            ? (last - start + 1u) : (0x4000u - start + last + 1u);
        if (delta < until_wrap) return (uint16_t)((start + delta) & 0x3FFFu);
        const uint32_t range = last - first + 1u;
        return (uint16_t)(first + ((delta - until_wrap) % range));
    };
    const auto read_row_table_raw = [&](uint16_t base, int row, bool big_endian) -> uint16_t {
        const uint16_t p = (uint16_t)((base + row * 2) & 0x3FFFu);
        const uint8_t b0 = avdc_.vram[p];
        const uint8_t b1 = avdc_.vram[(p + 1) & 0x3FFFu];
        return big_endian
            ? (uint16_t)(((uint16_t)b0 << 8) | b1)
            : (uint16_t)(((uint16_t)b1 << 8) | b0);
    };
    const auto read_row_table_ptr = [&](uint16_t base, int row, bool big_endian) -> uint16_t {
        return (uint16_t)(read_row_table_raw(base, row, big_endian) & 0x3FFFu);
    };
    if (use_row_table) {
        // SCN2674 row table entries are explicitly low byte followed by upper
        // byte.  Address zero is valid and must not be treated as an absent
        // entry.
        const uint16_t le0 = read_row_table_ptr(rowtbl_base, 0, false);
        const uint16_t be0 = read_row_table_ptr(rowtbl_base, 0, true);
        rowtbl_le_score = (le0 < 0x4000u) ? 1 : 0;
        rowtbl_be_score = (be0 < 0x4000u) ? 1 : 0;
        rowtbl_big_endian = false;

        int stride_hist[512] = {0};
        uint16_t prev = 0;
        bool have_prev = false;
        // Count valid rows within the chip's configured range only.
        const int configured_rows = std::min(avdc_rows, (int)avdc_.rows_per_screen);
        for (int row = 0; row < avdc_rows; row++) {
            const uint16_t line = read_row_table_ptr(rowtbl_base, row, rowtbl_big_endian);
            if (row < configured_rows && line < 0x4000u) {
                rowtbl_valid_lines++;
            }
            if (have_prev) {
                const int diff = (int)((line - prev) & 0x3FFFu);
                if ((diff > 0) && (diff < (int)(sizeof(stride_hist) / sizeof(stride_hist[0])))) {
                    stride_hist[diff]++;
                }
            }
            prev = line;
            have_prev = true;
        }
        for (int i = 1; i < (int)(sizeof(stride_hist) / sizeof(stride_hist[0])); i++) {
            if (stride_hist[i] > rowtbl_stride_hits) {
                rowtbl_stride_hits = stride_hist[i];
                rowtbl_stride = i;
            }
        }
    }
    constexpr int AVDC_GLYPH_ROWS = 11;
    const int AVDC_CELL_Y_SCALE = avdc_.interlace_enabled ? 1 : 2;
    const int avdc_logical_scanlines =
        std::clamp((int)avdc_.scanlines_per_char_row, 1, 30);
    const int AVDC_CELL_H_LOGICAL = avdc_logical_scanlines;
    const int avdc_char_w = std::clamp(text_dots_per_char, 7, 10);
    const int avdc_char_h_logical = AVDC_CELL_H_LOGICAL;
    const int avdc_char_h = avdc_char_h_logical * AVDC_CELL_Y_SCALE;
    // Use 24 MHz dot periods as the common 1056-pixel raster coordinate.
    // Thus 132x8 at 24 MHz occupies 1056 pixels, while the ROM's 80x9 at
    // 18 MHz occupies 960 pixels and is centered rather than stretched.
    const double avdc_clock_scale = 24000000.0 / (double)text_dot_clock;
    const int avdc_active_raster_w = std::max(
        1, (int)std::lround((double)(avdc_cols * avdc_char_w) * avdc_clock_scale));
    const double avdc_x_off = ((double)FULL_W - (double)avdc_active_raster_w) * 0.5;
    const int avdc_glyph_top = std::max(0, (avdc_char_h_logical - AVDC_GLYPH_ROWS) / 2);
    const int avdc_y_off = 0;
    const int avdc_underline_scan = std::clamp(
        (int)avdc_.underline_line, 0, avdc_logical_scanlines - 1);
    const bool avdc_text_blink_on = avdc_.character_blink_on;
    const bool avdc_cursor_blink_on = avdc_.cursor_blink_on;
    const auto avdc_output_line_address = [&](int combined_scanline) -> int {
        const int scan = std::clamp(combined_scanline, 0,
                                    avdc_logical_scanlines - 1);
        if (!avdc_.interlace_enabled) {
            return scan;
        }
        /* A complete-frame rendering interleaves even/odd field lines.  With
           sync-only interlace LA0..LA3 repeat the same line in both fields;
           with sync-and-video, ODD is the least-significant line bit. */
        return avdc_.interlace_sync_and_video ? scan : (scan >> 1);
    };
    const auto avdc_cursor_band = [&]() {
        const int start_scan = std::clamp(
            (int)avdc_.cursor_first_line, 0, avdc_logical_scanlines - 1);
        const int end_scan = std::clamp(
            (int)avdc_.cursor_last_line, 0, avdc_logical_scanlines - 1);
        return std::pair<int, int>{start_scan, end_scan};
    };
    const auto [avdc_cursor_scan0, avdc_cursor_scan1] = avdc_cursor_band();
    std::vector<uint16_t> rowtbl_line_bases;
    std::vector<uint8_t> rowtbl_double_modes((size_t)avdc_rows, 0u);
    if (use_row_table) {
        rowtbl_line_bases.resize((size_t)avdc_rows, linear_base);
        for (int row = 0; row < avdc_rows; row++) {
            const uint16_t raw = read_row_table_raw(rowtbl_base, row, rowtbl_big_endian);
            rowtbl_line_bases[(size_t)row] = (uint16_t)(raw & 0x3FFFu);
            rowtbl_double_modes[(size_t)row] = (uint8_t)((raw >> 14) & 0x03u);
        }
    }
    const uint16_t split_base = (uint16_t)(avdc_.start2_addr & 0x3FFFu);
    const int split1_row = (int)(avdc_.split_register[0] & 0x7Fu);
    const int split2_row = (int)(avdc_.split_register[1] & 0x7Fu);
    const int split1_switch_row =
        avdc_.split_use_screen2[0] ? split1_row : -1;
    const int split2_switch_row =
        avdc_.split_use_screen2[1]
            ? (split2_row + 1)
            : -1;
    std::vector<uint16_t> linear_line_bases((size_t)avdc_rows, linear_base);
    uint16_t active_linear_base = linear_base;
    int active_linear_origin_row = 0;
    for (int row = 0; row < avdc_rows; row++) {
        if ((row == split1_switch_row) || (row == split2_switch_row)) {
            active_linear_base = split_base;
            active_linear_origin_row = row;
        }
        linear_line_bases[(size_t)row] =
            wrap_display_addr(active_linear_base,
                              (uint32_t)(row - active_linear_origin_row) *
                              (uint32_t)avdc_stride *
                              (avdc_.gfx_enabled
                                  ? (uint32_t)(avdc_.interlace_enabled
                                      ? avdc_.scanlines_per_field_row
                                      : avdc_.scanlines_per_char_row)
                                  : 1u));
    }
    std::vector<uint8_t> chosen_row_double_modes((size_t)avdc_rows, 0u);
    const auto avdc_char_addr = [&](const std::vector<uint16_t>& line_bases, int row, int col) -> uint16_t {
        return wrap_display_addr(line_bases[(size_t)row], (uint32_t)col);
    };
    const auto map_avdc_x_span_to_disp = [&](int ax, int& dx0, int& dx1) {
        const double fx0 = avdc_x_off + (double)ax * avdc_clock_scale;
        const double fx1 = avdc_x_off + (double)(ax + 1) * avdc_clock_scale;
        dx0 = (int)std::floor(fx0 * (double)display::FB_W / (double)FULL_W);
        dx1 = (int)std::ceil(fx1 * (double)display::FB_W / (double)FULL_W);
        if (dx1 <= dx0) {
            dx1 = dx0 + 1;
        }
        dx0 = std::clamp(dx0, 0, display::FB_W);
        dx1 = std::clamp(dx1, 0, display::FB_W);
    };
    const auto map_scan_to_glyph_row = [&](int scanline) -> int {
        const int s = avdc_output_line_address(scanline);
        return s < AVDC_GLYPH_ROWS ? s : -1;
    };
    const auto map_scan_to_user_row16 = [&](int scanline) -> int {
        return std::clamp(avdc_output_line_address(scanline), 0, 15);
    };
    const auto apply_dot_stretch = [&](std::array<bool, 10>& row_dots,
                                       bool enabled) {
        if (!enabled || avdc_char_w < 2) {
            return;
        }
        const int dot_count = std::clamp(avdc_char_w, 1, (int)row_dots.size());
        const std::array<bool, 10> base = row_dots;
        for (int i = 1; i < dot_count; i++) {
            if (base[(size_t)(i - 1)]) {
                row_dots[(size_t)i] = true;
            }
        }
    };
    const auto draw_avdc_cell = [&](int col, int row, uint16_t off,
                                    uint16_t scanline_base) {
        const uint8_t row_double_mode =
            (row >= 0 && row < (int)chosen_row_double_modes.size())
                ? (uint8_t)(chosen_row_double_modes[(size_t)row] & 0x03u)
                : 0u;
        const bool row_double_width = row_double_mode != 0u;
        if (row_double_width && ((col & 1) != 0)) {
            return;
        }
        const int draw_col = row_double_width ? (col >> 1) : col;
        const int draw_char_w = row_double_width ? (avdc_char_w * 2) : avdc_char_w;

        const uint8_t ch = avdc_.vram[off];
        const uint8_t attr = avdc_.attr_vram[off];
        const bool attr_blink = (attr & 0x01u) != 0;
        const bool attr_underline = (attr & 0x02u) != 0;
        const bool attr_special = (attr & 0x04u) != 0;
        const bool attr_highlight = (attr & 0x10u) != 0;
        const bool attr_reverse = (attr & 0x20u) != 0;
        const bool attr_reverse_mono = !indexed_text_output && attr_reverse;
        uint8_t fg_idx = 0;
        uint8_t bg_idx = 0;
        int mono_fg = 160;
        int mono_bg = 0;
        if (indexed_text_output) {
            fg_idx = (uint8_t)(((attr_highlight ? 1u : 0u) << 2) |
                               ((text_b4 ? 1u : 0u) << 1) |
                               (text_b3 ? 1u : 0u));
            bg_idx = (uint8_t)((((attr & 0x80u) != 0u) ? 0x04u : 0x00u) |
                               (((attr & 0x20u) != 0u) ? 0x02u : 0x00u) |
                               (((attr & 0x40u) != 0u) ? 0x01u : 0x00u));
        } else {
            // Partner mono attribute logic:
            // base colors: black, green, bright green
            // b4: highlight -> fore=bright green
            // b5: reverse -> swap fore/back
            // b0: blink -> toggle green<->bright green over time, keep black unchanged
            constexpr int MONO_BLACK = 0;
            // Two mono text levels. MONO_HI must stay BELOW 240: the flat/color
            // shader paths treat framebuffer codes >=240 as RGBI colour indices
            // (0xF0..0xFF), so a highlight/inverse level of 248 was decoded as
            // colour index 8 = black, making highlight text and the highlighted
            // inverse block vanish. Keeping a clear gap also keeps the four
            // attribute states (normal / highlight / inverse / inverse+highlight)
            // visually distinct across green, orange, LCD and flat.
            constexpr int MONO_STD = 168;
            constexpr int MONO_HI = 232;
            constexpr int MONO_BG = 88;
            int fg_base = attr_highlight ? MONO_HI : MONO_STD;
            int bg_base = text_b4 ? MONO_BG : MONO_BLACK;
            if (attr_reverse_mono) {
                std::swap(fg_base, bg_base);
            }
            if (attr_blink) {
                const int blink_green = avdc_text_blink_on ? MONO_HI : MONO_STD;
                if (fg_base != MONO_BLACK) {
                    fg_base = blink_green;
                }
                if (bg_base != MONO_BLACK) {
                    bg_base = blink_green;
                }
            }
            mono_fg = fg_base;
            mono_bg = bg_base;
        }
        const bool has_visible_bg = indexed_text_output ? (bg_idx != 0u) : (mono_bg > 0);
        if (!avdc_.gfx_enabled && ch <= 0x20u && !attr_underline &&
            !attr_special && !attr_reverse_mono && !has_visible_bg) {
            return;
        }
        for (int ly = 0; ly < avdc_char_h_logical; ly++) {
            const int scan = (ly * avdc_logical_scanlines) / std::max(1, avdc_char_h_logical);
            int glyph_scan = scan;
            if (row_double_mode == 2u || row_double_mode == 3u) {
                const int half = std::max(1, avdc_logical_scanlines / 2);
                glyph_scan = (scan / 2) + ((row_double_mode == 3u) ? half : 0);
                glyph_scan = std::clamp(glyph_scan, 0, avdc_logical_scanlines - 1);
            }

            std::array<bool, 10> row_dots{};
            if (avdc_.gfx_enabled) {
                const int field_scan = avdc_.interlace_enabled
                    ? (scan >> 1) : scan;
                const uint16_t graphics_addr = wrap_display_addr(
                    off, (uint32_t)field_scan * (uint32_t)avdc_stride);
                const uint8_t src = avdc_.vram[graphics_addr];
                for (int px = 0; px < 8; px++) {
                    row_dots[(size_t)px] =
                        (src & (uint8_t)(0x80u >> px)) != 0u;
                }
                for (int px = 8;
                     px < std::min(avdc_char_w, (int)row_dots.size()); px++) {
                    row_dots[(size_t)px] = row_dots[7];
                }
            } else if (attr_special) {
                const int row16 = map_scan_to_user_row16(glyph_scan);
                const uint16_t user_addr = (uint16_t)((AVDC_UDG_BASE + ((uint16_t)(ch & 0x7Fu) * 16u) + (uint16_t)row16) & 0x3FFFu);
                const uint8_t src = avdc_.vram[user_addr];
                bool d[8]{};
                for (int i = 0; i < 8; i++) {
                    d[i] = ((src >> i) & 1u) != 0u;
                }
                // Partner board quirk: D0 and D7 are swapped only for user chars.
                std::swap(d[0], d[7]);
                // 9-dot output ordering: D7..D0 plus D8 where D8 is tied to D7.
                row_dots[0] = d[7];
                row_dots[1] = d[6];
                row_dots[2] = d[5];
                row_dots[3] = d[4];
                row_dots[4] = d[3];
                row_dots[5] = d[2];
                row_dots[6] = d[1];
                row_dots[7] = d[0];
                row_dots[8] = d[7];
                row_dots[9] = row_dots[8];
            } else {
                const int gry = map_scan_to_glyph_row(glyph_scan);
                if (gry >= 0 && gry < AVDC_GLYPH_ROWS) {
                    const uint8_t bits = avdc_.glyph_rom[ch][gry];
                    for (int px = 0; px < 8; px++) {
                        row_dots[px] = (bits & (uint8_t)(0x80u >> px)) != 0;
                    }
                    for (int px = 8; px < std::min(avdc_char_w, (int)row_dots.size()); px++) {
                        row_dots[px] = row_dots[7];
                    }
                }
            }
            /* The Partner connects ATTD3 to the CMAC DOTS pin.  DOTS is
               sampled once, on the falling edge of BLANK, and therefore
               applies to the entire following scan line rather than to the
               individual character whose attribute is being rendered.  At
               that boundary the AVDC sequencer has selected the first fetch
               address of the scan line. */
            const int field_scan = avdc_.interlace_enabled
                ? (scan >> 1) : scan;
            const uint16_t dot_control_addr = avdc_.gfx_enabled
                ? wrap_display_addr(scanline_base,
                    (uint32_t)field_scan * (uint32_t)avdc_stride)
                : scanline_base;
            const bool scanline_dot_stretch =
                (avdc_.attr_vram[dot_control_addr] & 0x08u) != 0u;
            apply_dot_stretch(row_dots, scanline_dot_stretch);
            const bool underline_row = attr_underline &&
                (avdc_output_line_address(scan) == avdc_underline_scan);
            for (int px = 0; px < draw_char_w; px++) {
                const int dot_x = row_double_width ? (px >> 1) : px;
                bool glyph_on = false;
                if (dot_x >= 0 && dot_x < (int)row_dots.size()) {
                    glyph_on = row_dots[(size_t)dot_x];
                }
                if (underline_row) {
                    glyph_on = true;
                }
                if (!text_color_mode && text_b3) {
                    glyph_on = false;
                }
                const int fx = draw_col * draw_char_w + px;
                int dx0 = 0;
                int dx1 = 0;
                map_avdc_x_span_to_disp(fx, dx0, dx1);
                for (int ydup = 0; ydup < AVDC_CELL_Y_SCALE; ydup++) {
                    const int fy = avdc_y_off + row * avdc_char_h + (ly * AVDC_CELL_Y_SCALE) + ydup;
                    int dy = 0;
                    dy = fy;
                    for (int dx = dx0; dx < dx1; dx++) {
                        if (indexed_text_output) {
                            if (glyph_on) {
                                disp.set_index_pixel(dx, dy, fg_idx);
                                avdc_pixels++;
                            } else if (bg_idx != 0u) {
                                disp.set_index_pixel(dx, dy, bg_idx);
                            }
                        } else {
                            const int draw_fg = mono_fg;
                            const int inten = glyph_on ? draw_fg : mono_bg;
                            const bool opaque_cell = (mono_bg > 0);
                            if (opaque_cell) {
                                disp.set_level_pixel(dx, dy, (uint8_t)std::clamp(inten, 0, 255));
                            } else if (glyph_on) {
                                if (draw_fg <= 0) {
                                    disp.set_level_pixel(dx, dy, 0);
                                } else {
                                    disp.add_pixel(dx, dy, (uint8_t)draw_fg);
                                }
                            }
                            if (glyph_on) {
                                avdc_pixels++;
                            }
                        }
                    }
                }
            }
        }
    };
    const auto draw_avdc_fallback_char = [&](int col, int row, uint8_t ch) {
        if (ch <= 0x20u) {
            return;
        }
        for (int ly = 0; ly < avdc_char_h_logical; ly++) {
            std::array<bool, 10> row_dots{};
            const int gry = ly - avdc_glyph_top;
            if (gry >= 0 && gry < AVDC_GLYPH_ROWS) {
                const uint8_t bits = avdc_.glyph_rom[ch][gry];
                for (int px = 0; px < 8; px++) {
                    row_dots[px] = (bits & (uint8_t)(0x80u >> px)) != 0;
                }
                for (int px = 8; px < std::min(avdc_char_w, (int)row_dots.size()); px++) {
                    row_dots[px] = row_dots[7];
                }
            }
            apply_dot_stretch(row_dots, false);
            for (int px = 0; px < avdc_char_w; px++) {
                if (!row_dots[(size_t)px]) {
                    continue;
                }
                const int fx = col * avdc_char_w + px;
                int dx0 = 0;
                int dx1 = 0;
                map_avdc_x_span_to_disp(fx, dx0, dx1);
                for (int ydup = 0; ydup < AVDC_CELL_Y_SCALE; ydup++) {
                    const int fy = avdc_y_off + row * avdc_char_h + (ly * AVDC_CELL_Y_SCALE) + ydup;
                    const int dy = fy;
                    for (int dx = dx0; dx < dx1; dx++) {
                        disp.add_pixel(dx, dy, 144);
                        avdc_pixels++;
                    }
                }
            }
        }
    };
    const auto draw_avdc_cursor = [&](int col, int row) {
        if (!avdc_.cursor_enabled || !avdc_cursor_blink_on) {
            return;
        }
        const uint8_t row_double_mode =
            (row >= 0 && row < (int)chosen_row_double_modes.size())
                ? (uint8_t)(chosen_row_double_modes[(size_t)row] & 0x03u)
                : 0u;
        const bool row_double_width = row_double_mode != 0u;
        if (row_double_width && ((col & 1) != 0)) {
            return;
        }
        const int draw_col = row_double_width ? (col >> 1) : col;
        const int draw_char_w = row_double_width ? (avdc_char_w * 2) : avdc_char_w;
        for (int ly = 0; ly < avdc_char_h_logical; ly++) {
            const int scan = (ly * avdc_logical_scanlines) / avdc_char_h_logical;
            const int output_scan = avdc_output_line_address(scan);
            if (output_scan < avdc_cursor_scan0 ||
                output_scan > avdc_cursor_scan1) {
                continue;
            }
            for (int px = 0; px < draw_char_w; px++) {
                const int fx = draw_col * draw_char_w + px;
                int dx0 = 0;
                int dx1 = 0;
                map_avdc_x_span_to_disp(fx, dx0, dx1);
                for (int ydup = 0; ydup < AVDC_CELL_Y_SCALE; ydup++) {
                    const int fy = avdc_y_off + row * avdc_char_h + (ly * AVDC_CELL_Y_SCALE) + ydup;
                    const int dy = fy;
                    for (int dx = dx0; dx < dx1; dx++) {
                        if (indexed_text_output) {
                            if (text_cursor_mode) {
                                /* CMODE=1 requests the SCB2675 white cursor. */
                                disp.set_index_pixel(dx, dy, 0x0F);
                            } else {
                                /* CMODE=0 logically inverts RGB cursor data. */
                                const uint8_t old = disp.data()[dx + dy * display::FB_W];
                                const uint8_t rgb = (old >= 0xF0u) ? (old & 0x0Fu) : 0u;
                                disp.set_index_pixel(dx, dy, (uint8_t)(rgb ^ 0x0Fu));
                            }
                        } else {
                            /* In monochrome mode CMODE is ignored; cursor is
                               reverse video, not an additive bright block. */
                            const uint8_t old = disp.data()[dx + dy * display::FB_W];
                            disp.set_level_pixel(dx, dy, old ? 0u : 242u);
                        }
                    }
                }
            }
        }
    };

    const auto count_visible_text = [&](const std::vector<uint16_t>& line_bases,
                                        int& visible_nonspace,
                                        int& visible_rows) {
        visible_nonspace = 0;
        visible_rows = 0;
        for (int row = 0; row < avdc_rows; row++) {
            int row_nonspace = 0;
            for (int col = 0; col < avdc_cols; col++) {
                const uint16_t base = avdc_char_addr(line_bases, row, col);
                bool visible = false;
                if (avdc_.gfx_enabled) {
                    const int scans = avdc_.interlace_enabled
                        ? avdc_.scanlines_per_field_row
                        : avdc_.scanlines_per_char_row;
                    for (int scan = 0; scan < scans && !visible; scan++) {
                        visible = avdc_.vram[wrap_display_addr(
                            base, (uint32_t)scan * (uint32_t)avdc_stride)] != 0u;
                    }
                } else {
                    visible = avdc_.vram[base] > 0x20u;
                }
                if (visible) {
                    visible_nonspace++;
                    row_nonspace++;
                }
            }
            if (row_nonspace > 0) {
                visible_rows++;
            }
        }
    };

    // Resolve row base addresses: trust the row table for all rows within
    // rows_per_screen (the chip's configured range), fall back to linear for
    // rows beyond that range. Use non-zero < 0x3F00 as the validity gate —
    // no lower-bound exclusion, because start1_addr=0 puts the first few
    // rows below 0x0100 legitimately.
    if (!rowtbl_line_bases.empty()) {
        const int chip_rows = std::min(avdc_rows, (int)avdc_.rows_per_screen);
        for (int row = 0; row < avdc_rows; row++) {
            if (row < chip_rows) {
                const uint16_t entry = read_row_table_ptr(rowtbl_base, row, rowtbl_big_endian);
                const bool valid = entry < 0x4000u;
                if (valid) {
                    rowtbl_line_bases[(size_t)row] = entry;
                } else {
                    rowtbl_line_bases[(size_t)row] = linear_line_bases[(size_t)row];
                }
            } else {
                // Beyond rows_per_screen: no row table entry exists.
                rowtbl_line_bases[(size_t)row] = linear_line_bases[(size_t)row];
                // Also clear double-mode — don't inherit garbage from post-table VRAM.
                if ((size_t)row < rowtbl_double_modes.size()) {
                    rowtbl_double_modes[(size_t)row] = 0;
                }
            }
        }
    }

    int rowtbl_visible_nonspace = 0;
    int rowtbl_visible_rows = 0;
    if (!rowtbl_line_bases.empty())
        count_visible_text(rowtbl_line_bases, rowtbl_visible_nonspace, rowtbl_visible_rows);

    int linear_visible_nonspace = 0;
    int linear_visible_rows = 0;
    count_visible_text(linear_line_bases, linear_visible_nonspace, linear_visible_rows);

    const bool rowtbl_configured = !rowtbl_line_bases.empty();
    const bool render_row_table = rowtbl_configured;
    avdc_visible_nonspace = render_row_table ? rowtbl_visible_nonspace
                                             : linear_visible_nonspace;
    const std::vector<uint16_t>& chosen_line_bases =
        render_row_table ? rowtbl_line_bases : linear_line_bases;
    const bool per_row_double_mode_enable = (avdc_.ir[0] & 0x80u) != 0;
    if (per_row_double_mode_enable) {
        if (render_row_table) {
            chosen_row_double_modes = rowtbl_double_modes;
        } else {
            std::fill(chosen_row_double_modes.begin(), chosen_row_double_modes.end(),
                      (uint8_t)((avdc_.ir[14] >> 6) & 0x03u));
        }
    } else {
        uint8_t mode = 0;
        const uint8_t split1_row = (uint8_t)(avdc_.split_register[0] & 0x7Fu);
        const uint8_t split2_row = (uint8_t)(avdc_.split_register[1] & 0x7Fu);
        const uint8_t split1_mode = (uint8_t)((avdc_.ir[14] >> 6) & 0x03u);
        const uint8_t split2_mode = (uint8_t)((avdc_.ir[14] >> 4) & 0x03u);
        for (int row = 0; row < avdc_rows; row++) {
            if (row == (int)split1_row) {
                mode = split1_mode;
            }
            if (row == (int)split2_row) {
                mode = split2_mode;
            }
            chosen_row_double_modes[(size_t)row] = mode;
            if (mode == 2u) {
                mode = 3u;
            } else if (mode == 3u) {
                mode = 2u;
            }
        }
    }

    const bool avdc_has_visible_text = (avdc_visible_nonspace > 0);
    const bool use_serial_fallback =
        serial_boot_text_available && (avdc_visible_nonspace < avdc_cols);

    if (render_trace_enabled()) {
        std::fprintf(stderr,
            "[render-avdc] rowtbl_cfg=%d rowtbl_use=%d rowtbl_base=%04x rowtbl_be=%d rowtbl_le=%d rowtbl_be_score=%d rowtbl_stride=%d rowtbl_valid=%d rowtbl_hits=%d linear_base=%04x rows=%d cols=%d stride=%d rowtbl_vis=%d rowtbl_rows=%d linear_vis=%d linear_rows=%d chosen_vis=%d any=%d serial_fb=%d\n",
            avdc_.use_row_table ? 1 : 0, render_row_table ? 1 : 0, rowtbl_base, rowtbl_big_endian ? 1 : 0,
            rowtbl_le_score, rowtbl_be_score, rowtbl_stride, rowtbl_valid_lines, rowtbl_stride_hits, linear_base,
            avdc_rows, avdc_cols, avdc_stride, rowtbl_visible_nonspace, rowtbl_visible_rows,
            linear_visible_nonspace, linear_visible_rows, avdc_visible_nonspace,
            avdc_nonspace, use_serial_fallback ? 1 : 0);
        auto dump_row_chars = [&](int row) {
            char buf[133];
            const int cols = std::min(avdc_cols, 132);
            for (int col = 0; col < cols; col++) {
                const uint8_t ch = avdc_.vram[avdc_char_addr(chosen_line_bases, row, col)];
                buf[col] = (ch >= 0x20 && ch < 0x7F) ? (char)ch : '.';
            }
            buf[cols] = '\0';
            std::fprintf(stderr, "[render-avdc] row%02d \"%s\"\n", row, buf);
        };
        for (int row = 0; row < std::min(avdc_rows, 6); row++) {
            dump_row_chars(row);
        }
        // Always dump the last two rows (setup/status rows 25-26).
        if (avdc_rows >= 2) {
            for (int row = avdc_rows - 2; row < avdc_rows; row++) {
                std::fprintf(stderr, "[render-avdc] setup row%02d base=%04x\n",
                    row, (unsigned)chosen_line_bases[(size_t)row]);
                dump_row_chars(row);
            }
        }
        if (!render_row_table) {
            std::fprintf(stderr, "[render-avdc] row05-bytes");
            for (int col = 0; col < std::min(avdc_cols, 48); col++) {
                const uint16_t off = avdc_char_addr(chosen_line_bases, 5, col);
                std::fprintf(stderr, " %02x", avdc_.vram[off]);
            }
            std::fprintf(stderr, "\n");
        }
    }

    if (use_serial_fallback) {
        std::string text = raw_serial_;
        const size_t cpm_pos = text.find("CP/M V3.0 Loader");
        if (cpm_pos != std::string::npos) {
            text.erase(0, cpm_pos);
        }
        std::vector<std::string> lines;
        std::string cur;
        for (char ch : text) {
            if (ch == '\r' || ch == '\n') {
                lines.push_back(cur);
                cur.clear();
            } else if ((unsigned char)ch >= 0x20 && (unsigned char)ch < 0x7F) {
                cur.push_back(ch);
            } else {
                cur.push_back(' ');
            }
        }
        if (!cur.empty()) {
            lines.push_back(cur);
        }
        const int keep = std::min(25, (int)lines.size());
        for (int row = 0; row < keep; row++) {
            const std::string &line = lines[(int)lines.size() - keep + row];
            for (int col = 0; col < std::min((int)line.size(), avdc_cols); col++) {
                draw_avdc_fallback_char(col, row, (uint8_t)line[col]);
            }
        }
    } else {
        if (avdc_has_visible_text) {
            for (int row = 0; row < avdc_rows; row++) {
                for (int col = 0; col < avdc_cols; col++) {
                    const uint16_t off = avdc_char_addr(chosen_line_bases, row, col);
                    draw_avdc_cell(col, row, off,
                                   chosen_line_bases[(size_t)row]);
                }
            }
        }
        const uint16_t cursor_addr = (uint16_t)(avdc_.cursor_addr & 0x3FFFu);
        bool cursor_drawn = false;
        for (int row = 0; row < avdc_rows && !cursor_drawn; row++) {
            for (int col = 0; col < avdc_cols; col++) {
                if (avdc_char_addr(chosen_line_bases, row, col) == cursor_addr) {
                    draw_avdc_cursor(col, row);
                    cursor_drawn = true;
                    break;
                }
            }
        }
    }

    (void)ef_on_pixels;
    (void)avdc_pixels;
}

bool partner_gdp::key_input(uint8_t ch)
{
    if (ch == '\n') {
        ch = '\r';
    }

    if (key_fifo_.size() >= KEY_FIFO_CAPACITY) {
        return false;
    }
    key_fifo_.push_back(ch);
    keyboard_.local_keypress();
    return true;
}

bool partner_gdp::keyboard_input_ready() const
{
    return z80sio_rx_enabled(&sio, Z80SIO_CHANNEL_A);
}

size_t partner_gdp::pending_key_count() const
{
    return key_fifo_.size() +
        (z80sio_line_rx_busy(&sio, Z80SIO_CHANNEL_A) ? 1u : 0u) +
        (z80sio_rx_ready(&sio, Z80SIO_CHANNEL_A) ? 1u : 0u);
}

std::string partner_gdp::dump_terminal_text() const
{
    return terminal_ ? terminal_->dump_text() : std::string{};
}

std::string partner_gdp::dump_raw_serial_text() const
{
    return raw_serial_;
}

void partner_gdp::gdp_newline()
{
    text_col_ = 0;
    text_row_++;
    if (text_row_ >= text_rows_)
        text_row_ = text_rows_ - 1;
    if (terminal_)
    {
        terminal_->put_char('\n');
        terminal_->put_char('\r');
    }
}

void partner_gdp::gdp_put_char(uint8_t ch)
{
    if (ch < 0x20)
    {
        if (ch == '\r' || ch == '\n')
            gdp_newline();
        return;
    }

    if (terminal_)
        terminal_->put_char(ch);
    raw_serial_.push_back((char)ch);
    text_col_++;
    if (text_col_ >= text_cols_)
        gdp_newline();
}

void partner_gdp::gdp_command(uint8_t cmd)
{
    switch (cmd)
    {
    case 0x08:
        if (text_col_ > 0)
            text_col_--;
        if (terminal_)
            terminal_->put_char(cmd);
        break;
    case 0x04: // CLS
    case 0x06: // CLS + XY=0
    case 0x07: // CLEAR
        text_col_ = 0;
        text_row_ = 0;
        if (terminal_)
            terminal_->reset();
        break;
    case 0x05: // Partner ROM uses this as X-home / left edge
        text_col_ = 0;
        break;
    case 0x0D: // X=0
        text_col_ = 0;
        break;
    case 0x0E: // Y=0
        text_row_ = 0;
        break;
    case 0x03:
        // ROM sends 03 00 05 01 as absolute positioning preamble.
        expect_abs_x_ = true;
        expect_abs_y_ = false;
        break;
    case 0x00:
        if (expect_abs_x_)
        {
            expect_abs_x_ = false;
            expect_abs_y_ = true;
        }
        else if (expect_abs_y_)
        {
            expect_abs_y_ = false;
        }
        break;
    case 0x0A:
    case 0x0B:
        // EF9367 block-draw commands advance the write position.
        text_col_++;
        if (text_col_ >= text_cols_)
            gdp_newline();
        break;
    default:
        break;
    }
}

uint8_t partner_gdp::io_read(uint16_t port)
{
    port &= 0xFF;
    const bool gdp_irq_active = (ef9367_pins_ & EF9367_IRQ) == 0;
    const bool avdc_irq_active = (avdc_pins_ & SCN2674_IRQ) != 0;

    if ((port >= 0x20 && port <= 0x2F))
    {
        io_cnt_.ef_rd++;
        return ef_bus_read(&ef9367_, (uint8_t)port, ef9367_pins_);
    }

    if (port == 0x30) {
        io_cnt_.pio_rd++;
        return gdp_pio_read(&gdp_video_pio_, 0,
                            gdp_irq_active, avdc_irq_active,
                            gdp_video_pio_pins_);
    }

    if (port >= 0x31 && port <= 0x33) {
        io_cnt_.pio_rd++;
        return gdp_pio_read(&gdp_video_pio_, (uint8_t)(port - 0x30),
                            gdp_irq_active, avdc_irq_active,
                            gdp_video_pio_pins_);
    }

    if (port == 0x36) {
        return avdc_restrict_ ? 0x10u : 0x00u;
    }

    if (port >= AVDC_BASE_PORT && port <= AVDC_LAST_PORT)
    {
        if (port == 0x34) return avdc_latch_read(&avdc_, false, avdc_pins_);
        if (port == 0x35) return avdc_latch_read(&avdc_, true, avdc_pins_);
        if (port == 0x36 || port == 0x37) return 0xFF;
        io_cnt_.avdc_rd++;
        return avdc_bus_read(&avdc_, (uint8_t)port, avdc_pins_);
    }

    return partner::io_read(port);
}

void partner_gdp::io_write(uint16_t port, uint8_t data)
{
    port &= 0xFF;
    const bool gdp_irq_active = (ef9367_pins_ & EF9367_IRQ) == 0;
    const bool avdc_irq_active = (avdc_pins_ & SCN2674_IRQ) != 0;

    if (port == EF9367_CMD_PORT)
    {
        io_cnt_.ef_wr++;
        ef_bus_write(&ef9367_, (uint8_t)port, data, ef9367_pins_);
        if (gdp_trace_enabled()) {
            std::fprintf(stderr,
                "[gdp-ef] pc=%04x cmd=%02x x=%u y=%u dx=%u dy=%u chsz=%02x cr1=%02x cr2=%02x scroll=%02x\n",
                cpu.pc, data, ef9367_.x, ef9367_.y, ef9367_.dx, ef9367_.dy,
                ef9367_.ch_size, ef9367_.cr1, ef9367_.cr2, gdp_scroll_);
        }
        // EF command space is 0x00..0x1F and 0x80..0xFF.
        // Printable glyph path is 0x20..0x7F.
        if ((data < 0x20) || (data >= 0x80))
            gdp_command(data);
        else
            gdp_put_char(data);
        return;
    }

    switch (port)
    {
    case EF9367_CR1_PORT:
        io_cnt_.ef_wr++;
        ef_bus_write(&ef9367_, (uint8_t)port, data, ef9367_pins_);
        if (gdp_trace_enabled()) {
            std::fprintf(stderr, "[gdp-ef] pc=%04x cr1<=%02x\n", cpu.pc, data);
        }
        return;
    case EF9367_CR2_PORT:
        io_cnt_.ef_wr++;
        ef_bus_write(&ef9367_, (uint8_t)port, data, ef9367_pins_);
        if (gdp_trace_enabled()) {
            std::fprintf(stderr, "[gdp-ef] pc=%04x cr2<=%02x\n", cpu.pc, data);
        }
        return;
    case EF9367_CHSZ_PORT:
        io_cnt_.ef_wr++;
        ef_bus_write(&ef9367_, (uint8_t)port, data, ef9367_pins_);
        if (gdp_trace_enabled()) {
            std::fprintf(stderr, "[gdp-ef] pc=%04x chsz<=%02x\n", cpu.pc, data);
        }
        return;
    case EF9367_DX_PORT: io_cnt_.ef_wr++; ef_bus_write(&ef9367_, (uint8_t)port, data, ef9367_pins_); return;
    case EF9367_DY_PORT: io_cnt_.ef_wr++; ef_bus_write(&ef9367_, (uint8_t)port, data, ef9367_pins_); return;
    case EF9367_XH_PORT:
        io_cnt_.ef_wr++; ef_bus_write(&ef9367_, (uint8_t)port, data, ef9367_pins_);
        return;
    case EF9367_XL_PORT:
        io_cnt_.ef_wr++;
        ef_bus_write(&ef9367_, (uint8_t)port, data, ef9367_pins_);
        text_col_ = (int)(ef9367_.x / 8u);
        if (text_col_ < 0) text_col_ = 0;
        if (text_col_ >= text_cols_) text_col_ = text_cols_ - 1;
        return;
    case EF9367_YH_PORT:
        io_cnt_.ef_wr++; ef_bus_write(&ef9367_, (uint8_t)port, data, ef9367_pins_);
        return;
    case EF9367_YL_PORT:
        io_cnt_.ef_wr++;
        ef_bus_write(&ef9367_, (uint8_t)port, data, ef9367_pins_);
        if (gdp_trace_enabled()) {
            std::fprintf(stderr, "[gdp-ef] pc=%04x y<=%u\n", cpu.pc, ef9367_.y);
        }
        text_row_ = (int)(ef9367_.y / 12u);
        if (text_row_ < 0) text_row_ = 0;
        if (text_row_ >= text_rows_) text_row_ = text_rows_ - 1;
        return;
    default:
        break;
    }

    if (port >= 0x30 && port <= 0x33)
    {
        io_cnt_.pio_wr++;
        gdp_pio_write(&gdp_video_pio_, (uint8_t)(port - 0x30), data,
                      gdp_irq_active, avdc_irq_active,
                      gdp_video_pio_pins_);
        const uint64_t board_pins =
            ef_board_pins_from_pio(Z80PIO_GET_PA(gdp_video_pio_pins_));
        ef9367_set_board_inputs(&ef9367_, board_pins);
        ef9367_pins_ = (ef9367_pins_ & ~EF_BOARD_INPUTS) |
                       (board_pins & EF_BOARD_INPUTS);
        return;
    }

    // GDP board external scroll latch (not EF9367 internal register).
    if (port == 0x36)
    {
        gdp_scroll_ = data;
        ef_scroll_latch_write(&ef9367_, data, ef9367_pins_);
        return;
    }

    if (port >= AVDC_BASE_PORT && port <= AVDC_LAST_PORT)
    {
        io_cnt_.avdc_wr++;
        const uint8_t pidx = (uint8_t)(port & 0x0F);
        avdc_port_wr_cnt_[pidx]++;
        if (port == 0x39) {
            avdc_cmd_cnt_[data]++;
        }
        if (port == 0x34) {
            avdc_char_wr_cnt_++;
            avdc_char_hist_[data]++;
            if (data > 0x20) {
                avdc_char_nonspace_wr_cnt_++;
                avdc_takeover_ = true;
            }
            if (avdc_trace_enabled()) {
                std::fprintf(stderr,
                    "[avdc] pc=%04x port=%02x char=%02x cur=%04x lat=%04x dirty=%d ptr=%04x s1=%04x s2=%04x\n",
                    cpu.pc, port, data, avdc_.cursor_addr, avdc_.addr_latch, avdc_.addr_latch_dirty ? 1 : 0,
                    avdc_.display_ptr_addr, avdc_.start1_addr, avdc_.start2_addr);
            }
            avdc_latch_write(&avdc_, false, data, avdc_pins_);
            return;
        }
        if (port == 0x35) {
            if (avdc_trace_enabled()) {
                std::fprintf(stderr,
                    "[avdc] pc=%04x port=%02x attr=%02x cur=%04x lat=%04x dirty=%d ptr=%04x s1=%04x s2=%04x\n",
                    cpu.pc, port, data, avdc_.cursor_addr, avdc_.addr_latch, avdc_.addr_latch_dirty ? 1 : 0,
                    avdc_.display_ptr_addr, avdc_.start1_addr, avdc_.start2_addr);
            }
            avdc_latch_write(&avdc_, true, data, avdc_pins_);
            return;
        }
        if (port == 0x36 || port == 0x37) {
            return;
        }
        if (avdc_trace_enabled()) {
            std::fprintf(stderr,
                "[avdc] pc=%04x port=%02x data=%02x cur=%04x lat=%04x dirty=%d ptr=%04x s1=%04x s2=%04x\n",
                cpu.pc, port, data, avdc_.cursor_addr, avdc_.addr_latch, avdc_.addr_latch_dirty ? 1 : 0,
                avdc_.display_ptr_addr, avdc_.start1_addr, avdc_.start2_addr);
        }
        avdc_bus_write(&avdc_, (uint8_t)port, data, avdc_pins_);
        return;
    }

    partner::io_write(port, data);
}
