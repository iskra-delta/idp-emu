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

static inline uint64_t ef_bus_idle() {
    return EF9367_CS | EF9367_RD | EF9367_WR | EF9367_RESET;
}

static inline uint8_t ef_bus_read(ef9367_t* chip, uint8_t port) {
    uint64_t pins = ef_bus_idle();
    pins |= (uint64_t)(port & 0x0F);
    pins &= ~(EF9367_CS | EF9367_RD);
    pins = ef9367_tick(chip, pins);
    return EF9367_GET_DATA(pins);
}

static inline void ef_bus_write(ef9367_t* chip, uint8_t port, uint8_t data) {
    uint64_t pins = ef_bus_idle();
    pins |= (uint64_t)(port & 0x0F);
    EF9367_SET_DATA(pins, data);
    pins &= ~(EF9367_CS | EF9367_WR);
    (void)ef9367_tick(chip, pins);
}

static inline uint64_t avdc_bus_idle() {
    return SCN2674_CS | SCN2674_RD | SCN2674_WR | SCN2674_RESET;
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

static inline uint8_t avdc_bus_read(scn2674_t* chip, uint8_t port) {
    const uint8_t idx = avdc_map_port_index(port);
    if (idx == 0xFF) return 0xFF;
    uint64_t pins = avdc_bus_idle();
    pins |= (uint64_t)(idx & 0x0F);
    pins &= ~(SCN2674_CS | SCN2674_RD);
    pins = scn2674_tick(chip, pins);
    return SCN2674_GET_DATA(pins);
}

static inline void avdc_bus_write(scn2674_t* chip, uint8_t port, uint8_t data) {
    const uint8_t idx = avdc_map_port_index(port);
    if (idx == 0xFF) return;
    uint64_t pins = avdc_bus_idle();
    pins |= (uint64_t)(idx & 0x0F);
    SCN2674_SET_DATA(pins, data);
    pins &= ~(SCN2674_CS | SCN2674_WR);
    (void)scn2674_tick(chip, pins);
}

static inline uint8_t gdp_pio_read(z80pio_t* pio, uint8_t port) {
    uint64_t pins = Z80PIO_CE | Z80PIO_IORQ | Z80PIO_RD;
    if (port & 0x01) pins |= Z80PIO_CDSEL;
    if (port & 0x02) pins |= Z80PIO_BASEL;
    pins = z80pio_tick(pio, pins);
    return Z80PIO_GET_DATA(pins);
}

static inline void gdp_pio_write(z80pio_t* pio, uint8_t port, uint8_t data) {
    uint64_t pins = Z80PIO_CE | Z80PIO_IORQ;
    if (port & 0x01) pins |= Z80PIO_CDSEL;
    if (port & 0x02) pins |= Z80PIO_BASEL;
    Z80PIO_SET_DATA(pins, data);
    (void)z80pio_tick(pio, pins);
}
}

partner_gdp::partner_gdp(terminal_profile profile, const std::string &rtc_nvram_path)
    : partner(rtc_nvram_path), terminal_profile_(profile)
{
    set_sio_port_lock(sio_port_id::sio1_a, true, "Internal GDP keyboard (fixed)");
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

void partner_gdp::sync_ef_mode_from_gdp_pio()
{
    const uint8_t pa = gdp_video_pio_.port[Z80PIO_PORT_A].output;
    // GDP video PIO port A wiring (per board notes/schematic):
    // A0=RBNK (display/read page), A1=WRNK (write page), A2=XORM.
    // A3/A4 are FORMAT0/FORMAT1 lines:
    //   00 -> 1024x256
    //   11 -> 1024x512
    // mixed states are transitional/undefined and keep prior mode.
    ef9367_.read_bank = (uint8_t)(pa & 0x01u);
    ef9367_.write_bank = (uint8_t)((pa >> 1) & 0x01u);
    ef9367_.xor_mode = (pa & 0x04u) != 0;
    const uint8_t fmt = (uint8_t)((pa >> 3) & 0x03u);
    if (fmt == 0x0u) {
        ef9367_.mode_512_lines = false;
    } else if (fmt == 0x3u) {
        ef9367_.mode_512_lines = true;
    } else {
        // Mixed FM0/FM1 states appear during transitions; follow FM0 line.
        ef9367_.mode_512_lines = (fmt & 0x01u) != 0u;
    }
}

void partner_gdp::reset()
{
    partner::reset();
    raw_serial_.clear();
    ef9367_reset(&ef9367_);
    std::memset(ef9367_.fb[0], 0, sizeof(ef9367_.fb[0]));
    std::memset(ef9367_.fb[1], 0, sizeof(ef9367_.fb[1]));
    scn2674_reset(&avdc_);
    z80pio_reset(&gdp_video_pio_);
    text_col_ = 0;
    text_row_ = 0;
    expect_abs_x_ = false;
    expect_abs_y_ = false;
    sync_div_ = 0;
    sync_bit4_ = false;
    io_cnt_ = {};
    avdc_port_wr_cnt_.fill(0);
    avdc_cmd_cnt_.fill(0);
    avdc_char_wr_cnt_ = 0;
    avdc_char_nonspace_wr_cnt_ = 0;
    avdc_char_hist_.fill(0);
    gdp_scroll_ = 0;
    avdc_takeover_ = false;
    avdc_mem_acc_phase_ = false;
    avdc_rowtbl_base_cache_ = 0;
    avdc_rowtbl_base_cache_valid_ = false;
    avdc_rowtbl_be_cache_ = false;
    avdc_rowtbl_bias_cache_ = 0;
    ef9367_.scroll_offset = 0;
    key_fifo_.clear();
    keyboard_.reset();
    sync_ef_mode_from_gdp_pio();
    if (terminal_)
        terminal_->reset();
}

void partner_gdp::tick()
{
    partner::tick();

    // Keep all serial TX channels drained so ROM/CP/M polling on TX-ready can
    // progress even when the GDP runtime moves console traffic to a different
    // SIO than the early bootstrap path.
    auto drain_tx = [&](z80sio_t& chip, int channel, bool keyboard_path) {
        if (chip.chn[channel].tx_ready)
            return;
        const uint8_t data = z80sio_tx_data(&chip, channel);
        if (keyboard_path) {
            keyboard_.host_write(data);
        }
    };
    // On Partner GDP, SIO traffic is keyboard/peripheral side traffic, not
    // video output. Drain transmit paths so firmware polling progresses, but
    // do not mirror any SIO TX bytes into the on-screen terminal/raw display
    // traces.
    drain_tx(sio, Z80SIO_CHANNEL_A, true);
    drain_tx(sio, Z80SIO_CHANNEL_B, false);
    drain_tx(sio2, Z80SIO_CHANNEL_A, false);
    drain_tx(sio2, Z80SIO_CHANNEL_B, false);

    // Feed queued keyboard input strictly through SIO RX path. Keyboard bytes
    // are hardware-serial input and must be consumed by firmware ISR/driver.
    if (!key_fifo_.empty()) {
        const uint8_t ch = key_fifo_.front();
        bool delivered = false;
        auto try_inject = [&](z80sio_t& chip, int channel) {
            if (chip.chn[channel].rx_ready)
                return;
            const bool was_ready = chip.chn[channel].rx_ready;
            z80sio_rx_data(&chip, channel, ch);
            delivered = delivered || (!was_ready && chip.chn[channel].rx_ready);
        };
        // On Partner GDP the keyboard is a serial device on the first SIO,
        // channel A. Keep the key path on that real channel so the SIO's RX
        // interrupt machinery remains responsible for delivering characters.
        try_inject(sio, Z80SIO_CHANNEL_A);
        if (delivered) {
            key_fifo_.pop_front();
        }
    }

    const uint64_t ef_idle = EF9367_CS | EF9367_RD | EF9367_WR | EF9367_RESET;
    const uint64_t avdc_idle = SCN2674_CS | SCN2674_RD | SCN2674_WR | SCN2674_RESET;

    ef9367_tick(&ef9367_, ef_idle);
    {
        const bool prev_vb = avdc_.prev_vblank_flag;
        scn2674_tick(&avdc_, avdc_idle);
        const bool cur_vb  = (avdc_.irq_live & 0x10u) != 0u;
        avdc_vb_edge_ = cur_vb && !prev_vb;
        // On each VB rising edge hold Z80_INT for 64 ticks. BIOS stage uses
        // I=0xFA and waits for >=1 non-space AVDC write (past cold-boot fill).
        // PartOS then hands off to its kernel IM2 table; recent builds place
        // that table on page 0xFE (older layouts used 0xFD). Keep VBL-driven
        // INT alive for either kernel page so the GDP scheduler and async FAT
        // worker continue to run after the ROM stage.
        if (avdc_vb_edge_) {
            if (cpu.i == 0xFE || cpu.i == 0xFD ||
                (cpu.i == 0xFA && avdc_char_nonspace_wr_cnt_ > 0))
                avdint_hold_ticks_ = 64;
        }
        if (avdint_hold_ticks_ > 0) avdint_hold_ticks_--;
    }
    (void)z80pio_tick(&gdp_video_pio_, 0);
    sync_ef_mode_from_gdp_pio();

    const bool pio_a_output = (gdp_video_pio_.port[Z80PIO_PORT_A].mode == Z80PIO_MODE_OUTPUT);
    const uint8_t pa = gdp_video_pio_.port[Z80PIO_PORT_A].output;
    // SCRLM appears active-low on Partner board wiring.
    const bool scroll_mode_enabled = !pio_a_output || ((pa & 0x80u) == 0u);
    ef9367_.scroll_offset = scroll_mode_enabled ? (int8_t)gdp_scroll_ : 0;
    sync_div_++;
    if (sync_div_ >= 1024) {
        sync_div_ = 0;
        sync_bit4_ = !sync_bit4_;
    }
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

    const uint8_t pa = gdp_video_pio_.port[Z80PIO_PORT_A].output;
    const bool pio_a_output = (gdp_video_pio_.port[Z80PIO_PORT_A].mode == Z80PIO_MODE_OUTPUT);
    // SCRLM appears active-low on Partner board wiring.
    const bool scroll_mode_enabled = !pio_a_output || ((pa & 0x80u) == 0u);

    const uint8_t text_ctl = gdp_video_pio_.port[Z80PIO_PORT_B].output;
    const bool text_dot_stretch = (text_ctl & 0x01u) != 0;
    const bool text_cursor_mode = (text_ctl & 0x02u) != 0;
    const bool text_color_mode = (text_ctl & 0x04u) != 0;
    const bool indexed_text_output =
        text_color_mode && (disp.get_phosphor_type() == display::phosphor_type::color);
    const bool text_b3 = (text_ctl & 0x08u) != 0; // mono: force background, color: blue fg
    const bool text_b4 = (text_ctl & 0x10u) != 0; // mono: reverse screen, color: green fg
    const int text_cc = (int)((text_ctl >> 5) & 0x03u); // C1:C0 dots/char selector
    const bool text_clk_24mhz = (text_ctl & 0x80u) != 0;

    // SCB2675B C1:C0 mapping: 00->10, 01->7, 10->8, 11->9 dots/char.
    int text_dots_per_char = 8;
    switch (text_cc) {
    case 0: text_dots_per_char = 10; break;
    case 1: text_dots_per_char = 7; break;
    case 2: text_dots_per_char = 8; break;
    case 3: text_dots_per_char = 9; break;
    default: break;
    }

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
        force_background = text_b3;
        reverse_video = text_b4;
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
    const int scroll = scroll_mode_enabled ? (int)(int8_t)gdp_scroll_ : 0;
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
            if (b & (uint8_t)(1u << (p & 7u))) {
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
    // Partner GDP presents a 26-row text raster even when the active CP/M
    // window uses only the top 24 rows. The bottom two rows are used for
    // setup/status content, so keep rendering the full physical 26-row space.
    const int avdc_rows = 26;
    const int avdc_cols_raw = std::min(132, std::max(1, (int)avdc_.chars_per_row));
    const bool avdc_wide_mode = text_clk_24mhz || (avdc_cols_raw > 80);
    const int avdc_cols = std::min(avdc_cols_raw, avdc_wide_mode ? 132 : 80);
    // VRAM stride must match chars_per_row (hardware layout), not the
    // display-capped avdc_cols — they differ when clock mode caps columns at 80.
    const int avdc_stride = std::max(1, (int)avdc_.chars_per_row);

    int avdc_nonspace = 0;
    for (uint8_t ch : avdc_.vram) {
        if (ch > 0x20) {
            avdc_nonspace++;
        }
    }
    int avdc_visible_nonspace = 0;

    bool use_row_table = avdc_.use_row_table;
    uint16_t rowtbl_base =
        (avdc_.start2_addr_start & 0x3FFFu) ? (uint16_t)(avdc_.start2_addr_start & 0x3FFFu)
                                            : (uint16_t)(avdc_.start2_addr & 0x3FFFu);
    bool rowtbl_big_endian = false;
    int rowtbl_le_score = 0;
    int rowtbl_be_score = 0;
    int rowtbl_stride = avdc_stride;
    int rowtbl_valid_lines = 0;
    int rowtbl_stride_hits = 0;
    const uint16_t linear_base =
        (avdc_.start1_addr & 0x3FFFu) ? (uint16_t)(avdc_.start1_addr & 0x3FFFu)
                                      : (uint16_t)(avdc_.display_ptr_addr & 0x3FFFu);
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
        // Endian detection: a non-zero little-endian read at row 0 is the
        // strong signal. If it reads as zero (e.g. uninitialised table) but
        // big-endian is plausible, fall back. Using < 0x3F00 only — no lower
        // bound — because start1_addr=0 legitimately puts early rows below
        // 0x0100 (row 0 text at 0x0034, row 1 at 0x0084, etc.).
        const uint16_t le0 = read_row_table_ptr(rowtbl_base, 0, false);
        const uint16_t be0 = read_row_table_ptr(rowtbl_base, 0, true);
        const bool le0_valid = (le0 != 0u) && (le0 < 0x3F00u);
        const bool be0_valid = (be0 != 0u) && (be0 < 0x3F00u);
        rowtbl_le_score = le0_valid ? 1 : 0;
        rowtbl_be_score = be0_valid ? 1 : 0;
        rowtbl_big_endian = (!le0_valid && be0_valid);

        int stride_hist[512] = {0};
        uint16_t prev = 0;
        bool have_prev = false;
        // Count valid rows within the chip's configured range only.
        const int configured_rows = std::min(avdc_rows, (int)avdc_.rows_per_screen);
        for (int row = 0; row < avdc_rows; row++) {
            const uint16_t line = read_row_table_ptr(rowtbl_base, row, rowtbl_big_endian);
            // Valid = non-zero address within VRAM, AND within rows_per_screen.
            if (row < configured_rows && (line != 0u) && (line < 0x3F00u)) {
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
    constexpr int AVDC_CELL_Y_SCALE = 2;
    constexpr int AVDC_GLYPH_ROWS = 11;
    const int avdc_logical_scanlines = std::clamp((int)avdc_.scanlines_per_char_row, 8, 16);
    const int AVDC_CELL_H_LOGICAL = std::max(AVDC_GLYPH_ROWS, std::clamp(avdc_logical_scanlines, 8, 12));
    const int avdc_char_w = std::clamp(text_dots_per_char, 7, 10);
    const int avdc_char_h_logical = AVDC_CELL_H_LOGICAL;
    const int avdc_char_h = avdc_char_h_logical * AVDC_CELL_Y_SCALE;
    const int avdc_raster_w = std::max(1, avdc_cols * avdc_char_w);
    const int avdc_glyph_top = std::max(0, (avdc_char_h_logical - AVDC_GLYPH_ROWS) / 2);
    const int avdc_y_off = 0;
    const int avdc_cursor_span = std::clamp((int)(avdc_.ir[6] & 0x0F), 1, avdc_logical_scanlines);
    const int avdc_underline_scan = std::clamp((int)(avdc_.ir[7] & 0x0F), 0, avdc_logical_scanlines - 1);
    const bool avdc_text_blink_enabled = (avdc_.ir[4] & 0x80u) != 0;
    // Blink phase must be driven by *emulated* time, not host wall-clock:
    // using steady_clock here made the GDP render nondeterministic (a screen
    // dump could catch blinking cells in their "off" phase and read blank),
    // which showed up as spurious ~10% "no shell prompt" boot failures in the
    // partner_gdp golden tests. The Partner CPU runs at 4 MHz => 4000 ticks/ms.
    const uint64_t now_ms = get_tick_count() / 4000u;
    // Character blink is field-rate based on IR4[7]: 1/64 or 1/128 VSYNC.
    const double blink_frames = ((avdc_.ir[4] & 0x80u) != 0u) ? 128.0 : 64.0;
    const double blink_period_ms_f = std::max(1.0, (blink_frames / 60.0) * 1000.0);
    const uint64_t avdc_blink_period_ms = (uint64_t)blink_period_ms_f;
    const uint64_t avdc_blink_phase = (avdc_blink_period_ms > 0u)
        ? (now_ms / avdc_blink_period_ms)
        : 0u;
    const bool avdc_text_blink_on =
        !avdc_text_blink_enabled || ((avdc_blink_phase & 1u) == 0u);
    const bool cursor_blink_enabled = (avdc_.ir[7] & 0x20u) != 0u;
    const double cursor_frames = ((avdc_.ir[7] & 0x10u) != 0u) ? 64.0 : 32.0;
    const uint64_t cursor_period_ms =
        (uint64_t)std::max(1.0, (cursor_frames / 60.0) * 1000.0);
    const bool avdc_cursor_blink_on =
        !cursor_blink_enabled || (((now_ms / cursor_period_ms) & 1u) == 0u);
    const auto avdc_cursor_band = [&]() {
        int start_scan = 0;
        int end_scan = avdc_logical_scanlines - 1;
        if (text_cursor_mode) {
            start_scan = 0;
            end_scan = avdc_logical_scanlines - 1;
        } else if (avdc_cursor_span <= 1) {
            start_scan = avdc_underline_scan;
            end_scan = avdc_underline_scan;
        } else {
            start_scan = std::max(0, avdc_logical_scanlines - avdc_cursor_span);
            end_scan = avdc_logical_scanlines - 1;
        }
        return std::pair<int, int>{start_scan, end_scan};
    };
    const auto [avdc_cursor_scan0, avdc_cursor_scan1] = avdc_cursor_band();
    std::vector<uint16_t> rowtbl_line_bases;
    std::vector<uint8_t> rowtbl_double_modes;
    if (use_row_table) {
        rowtbl_line_bases.resize((size_t)avdc_rows, linear_base);
        rowtbl_double_modes.resize((size_t)avdc_rows, 0);
        for (int row = 0; row < avdc_rows; row++) {
            const uint16_t raw = read_row_table_raw(rowtbl_base, row, rowtbl_big_endian);
            rowtbl_line_bases[(size_t)row] = (uint16_t)(raw & 0x3FFFu);
            rowtbl_double_modes[(size_t)row] = (uint8_t)((raw >> 14) & 0x03u);
        }
    }
    const uint16_t split_base = (uint16_t)(avdc_.start2_addr & 0x3FFFu);
    const bool scrolling = avdc_.scroll_start && avdc_.scroll_end;
    const int split1_row = (int)(avdc_.split_register[0] & 0x7Fu);
    const int split2_row = (int)(avdc_.split_register[1] & 0x7Fu);
    const int split1_switch_row =
        (avdc_.split_use_screen2[0] && split_base != 0u) ? split1_row : -1;
    const int split2_switch_row =
        (avdc_.split_use_screen2[1] && split_base != 0u)
            ? (split2_row + (scrolling ? 0 : 1))
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
            (uint16_t)((active_linear_base + (row - active_linear_origin_row) * avdc_stride) & 0x3FFFu);
    }
    std::vector<uint8_t> chosen_row_double_modes((size_t)avdc_rows, 0);
    const auto avdc_char_addr = [&](const std::vector<uint16_t>& line_bases, int row, int col) -> uint16_t {
        return (uint16_t)((line_bases[(size_t)row] + col) & 0x3FFFu);
    };
    const auto map_avdc_x_span_to_disp = [&](int ax, int& dx0, int& dx1) {
        dx0 = (ax * FULL_W) / avdc_raster_w;
        dx1 = ((ax + 1) * FULL_W) / avdc_raster_w;
        if (dx1 <= dx0) {
            dx1 = dx0 + 1;
        }
        dx0 = std::clamp(dx0, 0, display::FB_W);
        dx1 = std::clamp(dx1, 0, display::FB_W);
    };
    const auto map_scan_to_glyph_row = [&](int scanline) -> int {
        const int s = std::clamp(scanline, 0, std::max(0, avdc_logical_scanlines - 1));
        if (avdc_logical_scanlines <= 1) {
            return 0;
        }
        const int num = s * (AVDC_GLYPH_ROWS - 1) + ((avdc_logical_scanlines - 1) / 2);
        const int den = avdc_logical_scanlines - 1;
        return std::clamp(num / den, 0, AVDC_GLYPH_ROWS - 1);
    };
    const auto map_scan_to_user_row16 = [&](int scanline) -> int {
        const int s = std::clamp(scanline, 0, std::max(0, avdc_logical_scanlines - 1));
        if (avdc_logical_scanlines <= 1) {
            return 0;
        }
        const int num = s * 15 + ((avdc_logical_scanlines - 1) / 2);
        const int den = avdc_logical_scanlines - 1;
        return std::clamp(num / den, 0, 15);
    };
    const auto apply_dot_stretch = [&](std::array<bool, 10>& row_dots) {
        if (!text_dot_stretch || avdc_char_w < 2) {
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
    const auto draw_avdc_cell = [&](int col, int row, uint16_t off) {
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
            int fg_base = attr_highlight ? MONO_HI : MONO_STD;
            int bg_base = MONO_BLACK;
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
        if (ch <= 0x20u && !attr_underline && !attr_special && !attr_reverse_mono && !has_visible_bg) {
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
            if (attr_special) {
                const int row16 = map_scan_to_user_row16(glyph_scan);
                const uint16_t user_addr = (uint16_t)((0x1000u + ((uint16_t)(ch & 0x7Fu) * 16u) + (uint16_t)row16) & 0x3FFFu);
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
            apply_dot_stretch(row_dots);
            const bool underline_row = attr_underline && (scan == avdc_underline_scan);
            for (int px = 0; px < draw_char_w; px++) {
                const int dot_x = row_double_width ? (px >> 1) : px;
                bool glyph_on = false;
                if (dot_x >= 0 && dot_x < (int)row_dots.size()) {
                    glyph_on = row_dots[(size_t)dot_x];
                }
                if (underline_row) {
                    glyph_on = true;
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
            apply_dot_stretch(row_dots);
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
            if (scan < avdc_cursor_scan0 || scan > avdc_cursor_scan1) {
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
                            disp.set_index_pixel(dx, dy, 0x0F);
                        } else {
                            disp.add_pixel(dx, dy, 242);
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
                if (avdc_.vram[avdc_char_addr(line_bases, row, col)] > 0x20) {
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
                const bool valid = (entry != 0u) && (entry < 0x3F00u);
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

    const bool rowtbl_configured =
        !rowtbl_line_bases.empty() &&
        (rowtbl_valid_lines >= std::max(4, avdc_rows / 2));
    const bool render_row_table = rowtbl_configured;
    avdc_visible_nonspace = render_row_table ? rowtbl_visible_nonspace
                                             : linear_visible_nonspace;
    const std::vector<uint16_t>& chosen_line_bases =
        render_row_table ? rowtbl_line_bases : linear_line_bases;
    const bool per_row_double_mode_enable = (avdc_.ir[0] & 0x80u) != 0;
    if (per_row_double_mode_enable) {
        if (render_row_table && !rowtbl_double_modes.empty()) {
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
                    draw_avdc_cell(col, row, off);
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

    const bool was_ready = sio.chn[Z80SIO_CHANNEL_A].rx_ready;
    if (!was_ready) {
        z80sio_rx_data(&sio, Z80SIO_CHANNEL_A, ch);
        if (!was_ready && sio.chn[Z80SIO_CHANNEL_A].rx_ready) {
            keyboard_.local_keypress();
            return true;
        }
    }
    if (key_fifo_.size() >= KEY_FIFO_CAPACITY) {
        return false;
    }
    key_fifo_.push_back(ch);
    keyboard_.local_keypress();
    return true;
}

size_t partner_gdp::pending_key_count() const
{
    return key_fifo_.size() +
        (sio.chn[Z80SIO_CHANNEL_A].rx_ready ? 1u : 0u);
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
    case 0x05: // Partner GDP uses this as X-home / left edge
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

    // GDP common-control port is wired as a mirror of GDP-board PIO port A.
    // Bits 5/6 are board interrupt inputs (read-only from CPU perspective).
    auto read_common_ctrl_port = [&]() -> uint8_t {
        uint8_t value = gdp_video_pio_.port[Z80PIO_PORT_A].output;
        // GDPINT is modeled as EF vertical-blank status activity.
        const bool gdpint = ef9367_.vblank;
        // AVDINT follows the hardware INT pin: only high when an unmasked
        // interrupt is pending. The boot code polls this bit and expects it
        // LOW until the BIOS enables AVDC interrupts.
        const bool avdint = (avdc_.irq_status != 0u);
        value = (uint8_t)((value & ~(0x20u | 0x40u)) |
                          (gdpint ? 0x20u : 0x00u) |
                          (avdint ? 0x40u : 0x00u));
        return value;
    };

    if ((port >= 0x20 && port <= 0x2F))
    {
        io_cnt_.ef_rd++;
        return ef_bus_read(&ef9367_, (uint8_t)port);
    }

    if (port == 0x30) {
        io_cnt_.pio_rd++;
        // Keep PIO timing/state progression in sync with CPU reads.
        (void)gdp_pio_read(&gdp_video_pio_, 0);
        return read_common_ctrl_port();
    }

    if (port >= 0x31 && port <= 0x33) {
        io_cnt_.pio_rd++;
        return gdp_pio_read(&gdp_video_pio_, (uint8_t)(port - 0x30));
    }

    if (port == 0x36) {
        // The GDP AVDC driver polls MEM_ACC as a short handshake pulse:
        // first wait for bit4 high, then for it to drop low again. Modeling
        // that as a long tick-based square wave makes every text-cell access
        // artificially stall for thousands of CPU ticks, so large outputs like
        // `ls` appear frozen. Keep the handshake edge-based instead.
        avdc_mem_acc_phase_ = !avdc_mem_acc_phase_;
        return avdc_mem_acc_phase_ ? 0x10 : 0x00;
    }

    if (port >= AVDC_BASE_PORT && port <= AVDC_LAST_PORT)
    {
        if (port == 0x34) return avdc_.char_latch;
        if (port == 0x35) return avdc_.attr_latch;
        if (port == 0x36 || port == 0x37) return 0xFF;
        io_cnt_.avdc_rd++;
        return avdc_bus_read(&avdc_, (uint8_t)port);
    }

    return partner::io_read(port);
}

int partner_gdp::get_external_im2_vector() const
{
    // Assert INT for 64 ticks on each AVDC VB edge. 64 ticks covers the longest
    // Z80 instruction (23 cycles) so the Z80 reliably catches the interrupt.
    // The handler runs under DI so INT going low mid-handler is harmless.
    if (avdint_hold_ticks_ > 0) {
        return 0x8E;
    }
    return -1;
}

void partner_gdp::io_write(uint16_t port, uint8_t data)
{
    port &= 0xFF;

    if (port == EF9367_CMD_PORT)
    {
        io_cnt_.ef_wr++;
        ef_bus_write(&ef9367_, (uint8_t)port, data);
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
        ef_bus_write(&ef9367_, (uint8_t)port, data);
        if (gdp_trace_enabled()) {
            std::fprintf(stderr, "[gdp-ef] pc=%04x cr1<=%02x\n", cpu.pc, data);
        }
        return;
    case EF9367_CR2_PORT:
        io_cnt_.ef_wr++;
        ef_bus_write(&ef9367_, (uint8_t)port, data);
        if (gdp_trace_enabled()) {
            std::fprintf(stderr, "[gdp-ef] pc=%04x cr2<=%02x\n", cpu.pc, data);
        }
        return;
    case EF9367_CHSZ_PORT:
        io_cnt_.ef_wr++;
        ef_bus_write(&ef9367_, (uint8_t)port, data);
        if (gdp_trace_enabled()) {
            std::fprintf(stderr, "[gdp-ef] pc=%04x chsz<=%02x\n", cpu.pc, data);
        }
        return;
    case EF9367_DX_PORT: io_cnt_.ef_wr++; ef_bus_write(&ef9367_, (uint8_t)port, data); return;
    case EF9367_DY_PORT: io_cnt_.ef_wr++; ef_bus_write(&ef9367_, (uint8_t)port, data); return;
    case EF9367_XH_PORT:
        io_cnt_.ef_wr++; ef_bus_write(&ef9367_, (uint8_t)port, data);
        return;
    case EF9367_XL_PORT:
        io_cnt_.ef_wr++;
        ef_bus_write(&ef9367_, (uint8_t)port, data);
        text_col_ = (int)(ef9367_.x / 8u);
        if (text_col_ < 0) text_col_ = 0;
        if (text_col_ >= text_cols_) text_col_ = text_cols_ - 1;
        return;
    case EF9367_YH_PORT:
        io_cnt_.ef_wr++; ef_bus_write(&ef9367_, (uint8_t)port, data);
        return;
    case EF9367_YL_PORT:
        io_cnt_.ef_wr++;
        ef_bus_write(&ef9367_, (uint8_t)port, data);
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
        gdp_pio_write(&gdp_video_pio_, (uint8_t)(port - 0x30), data);
        sync_ef_mode_from_gdp_pio();
        return;
    }

    // GDP board external scroll latch (not EF9367 internal register).
    if (port == 0x36)
    {
        gdp_scroll_ = data;
        const bool pio_a_output = (gdp_video_pio_.port[Z80PIO_PORT_A].mode == Z80PIO_MODE_OUTPUT);
        const uint8_t pa = gdp_video_pio_.port[Z80PIO_PORT_A].output;
    // SCRLM appears active-low on Partner board wiring.
    const bool scroll_mode_enabled = !pio_a_output || ((pa & 0x80u) == 0u);
        ef9367_.scroll_offset = scroll_mode_enabled ? (int8_t)gdp_scroll_ : 0;
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
            avdc_.char_latch = data;
            return;
        }
        if (port == 0x35) {
            if (avdc_trace_enabled()) {
                std::fprintf(stderr,
                    "[avdc] pc=%04x port=%02x attr=%02x cur=%04x lat=%04x dirty=%d ptr=%04x s1=%04x s2=%04x\n",
                    cpu.pc, port, data, avdc_.cursor_addr, avdc_.addr_latch, avdc_.addr_latch_dirty ? 1 : 0,
                    avdc_.display_ptr_addr, avdc_.start1_addr, avdc_.start2_addr);
            }
            avdc_.attr_latch = data;
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
        avdc_bus_write(&avdc_, (uint8_t)port, data);
        return;
    }

    partner::io_write(port, data);
}
