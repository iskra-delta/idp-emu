#include "partner_gdp.hpp"
#include "gui/display.hpp"
#include "ef9367_font.hpp"
#include "scn2674_font.hpp"
#include <algorithm>
#include <chrono>
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

partner_gdp::partner_gdp(terminal_profile profile) : terminal_profile_(profile)
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
    // A0=RBNK (read bank), A1=WRNK (write bank), A2=XOR draw mode,
    // A3=resolution (0=1024x256, 1=1024x512).
    ef9367_.read_bank = (uint8_t)(pa & 0x01u);
    ef9367_.write_bank = (uint8_t)((pa >> 1) & 0x01u);
    ef9367_.xor_mode = (pa & 0x04u) != 0;
    ef9367_.mode_512_lines = (pa & 0x08u) != 0;
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
    avdc_rowtbl_base_cache_ = 0;
    avdc_rowtbl_base_cache_valid_ = false;
    avdc_rowtbl_be_cache_ = false;
    avdc_rowtbl_bias_cache_ = 0;
    ef9367_.scroll_offset = 0;
    key_fifo_.clear();
    bios_key_write_ptr_ = 0;
    keyboard_.reset();
    sync_ef_mode_from_gdp_pio();
    if (terminal_)
        terminal_->reset();
}

void partner_gdp::tick()
{
    partner::tick();
    maybe_auto_boot_floppy();

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

    // Feed queued keyboard input only when the SIO receive latch is ready.
    // This avoids dropping bytes when CP/M is between polling windows.
    if (!key_fifo_.empty()) {
        const uint8_t ch = key_fifo_.front();
        bool delivered = false;
        if (late_bios_queue_active()) {
            delivered = enqueue_late_bios_key(ch);
        }
        auto try_inject = [&](z80sio_t& chip, int channel) {
            if (chip.chn[channel].rx_ready)
                return;
            const bool was_ready = chip.chn[channel].rx_ready;
            z80sio_rx_data(&chip, channel, ch);
            if (!chip.chn[channel].rx_ready && !was_ready) {
                chip.chn[channel].rx_data = ch;
                chip.chn[channel].rx_ready = true;
                chip.chn[channel].int_state |= Z80SIO_INT_NEEDED;
            }
            delivered = delivered || chip.chn[channel].rx_ready;
        };
        // On Partner GDP the keyboard is a serial device on the first SIO,
        // channel A. Keep the key path on that real channel so the SIO's RX
        // interrupt machinery remains responsible for delivering characters.
        if (!delivered) {
            try_inject(sio, Z80SIO_CHANNEL_A);
        }
        if (delivered) {
            key_fifo_.pop_front();
        }
    }

    const uint64_t ef_idle = EF9367_CS | EF9367_RD | EF9367_WR | EF9367_RESET;
    const uint64_t avdc_idle = SCN2674_CS | SCN2674_RD | SCN2674_WR | SCN2674_RESET;
    ef9367_tick(&ef9367_, ef_idle);
    scn2674_tick(&avdc_, avdc_idle);
    (void)z80pio_tick(&gdp_video_pio_, 0);
    sync_ef_mode_from_gdp_pio();
    ef9367_.scroll_offset = (int8_t)gdp_scroll_;
    sync_div_++;
    if (sync_div_ >= 1024) {
        sync_div_ = 0;
        sync_bit4_ = !sync_bit4_;
    }
}

bool partner_gdp::late_bios_queue_active() const
{
    if (rom_enabled) {
        return false;
    }
    if (cpu.im != 2 || cpu.i != 0xFA) {
        return false;
    }
    const uint8_t queued = peek_ram(0xFF1A);
    const uint8_t write_ptr = peek_ram(0xFF23);
    return (queued <= 8u) && (write_ptr >= 0x1Bu) && (write_ptr <= 0x22u);
}

bool partner_gdp::enqueue_late_bios_key(uint8_t ch)
{
    uint8_t queued = peek_ram(0xFF1A);
    if (queued >= 8u) {
        return false;
    }

    uint8_t write_ptr = peek_ram(0xFF23);
    if (write_ptr < 0x1Bu || write_ptr > 0x22u) {
        if (bios_key_write_ptr_ >= 0xFF1Bu && bios_key_write_ptr_ <= 0xFF22u) {
            write_ptr = (uint8_t)(bios_key_write_ptr_ & 0xFFu);
        } else {
            write_ptr = 0x1Bu;
        }
        write_mem(0xFF23u, write_ptr);
    }

    write_mem((uint16_t)(0xFF00u | write_ptr), ch);
    const uint8_t next_write_ptr = (write_ptr >= 0x22u) ? 0x1Bu : (uint8_t)(write_ptr + 1u);
    write_mem(0xFF23u, next_write_ptr);
    write_mem(0xFF1Au, (uint8_t)(queued + 1u));
    bios_key_write_ptr_ = (uint16_t)(0xFF00u | next_write_ptr);

    if (gdp_trace_enabled()) {
        std::fprintf(stderr,
            "[gdp-keyq] pc=%04x ch=%02x queued=%u write=%02x next=%02x\n",
            cpu.pc, ch, queued, write_ptr, next_write_ptr);
    }
    return true;
}

void partner_gdp::render_to(display &disp)
{
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

    const auto map_full_to_disp = [](int fx, int fy, int &dx, int &dy) {
        dx = (fx * display::FB_W) / FULL_W;
        dy = (fy * display::FB_H) / FULL_H;
    };

    const uint8_t *ef_page = ef9367_.fb[ef9367_.read_bank & 1u];
    const int scroll = (int8_t)gdp_scroll_;
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
    const int avdc_rows = std::min(26, std::max(1, (int)avdc_.rows_per_screen));
    const int avdc_cols = std::min(132, std::max(1, (int)avdc_.chars_per_row));
    const int avdc_stride = avdc_cols;

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
    int rowtbl_bias = 0;
    int rowtbl_le_score = 0;
    int rowtbl_be_score = 0;
    int rowtbl_stride = avdc_stride;
    int rowtbl_valid_lines = 0;
    int rowtbl_stride_hits = 0;
    const uint16_t linear_base =
        (avdc_.start1_addr & 0x3FFFu) ? (uint16_t)(avdc_.start1_addr & 0x3FFFu)
                                      : (uint16_t)(avdc_.display_ptr_addr & 0x3FFFu);
    const auto read_row_table_ptr = [&](uint16_t base, int row, bool big_endian) -> uint16_t {
        const uint16_t p = (uint16_t)((base + row * 2) & 0x3FFFu);
        const uint8_t b0 = avdc_.vram[p];
        const uint8_t b1 = avdc_.vram[(p + 1) & 0x3FFFu];
        uint16_t line = big_endian
            ? (uint16_t)(((uint16_t)b0 << 8) | b1)
            : (uint16_t)(((uint16_t)b1 << 8) | b0);
        return (uint16_t)(line & 0x3FFFu);
    };
    if (use_row_table) {
        const uint16_t le0 = read_row_table_ptr(rowtbl_base, 0, false);
        const uint16_t be0 = read_row_table_ptr(rowtbl_base, 0, true);
        const bool le0_valid = (le0 >= 0x0100u) && (le0 < 0x3F00u);
        const bool be0_valid = (be0 >= 0x0100u) && (be0 < 0x3F00u);
        rowtbl_le_score = le0_valid ? 1 : 0;
        rowtbl_be_score = be0_valid ? 1 : 0;
        rowtbl_big_endian = (!le0_valid && be0_valid);

        int stride_hist[512] = {0};
        uint16_t prev = 0;
        bool have_prev = false;
        for (int row = 0; row < avdc_rows; row++) {
            const uint16_t line = read_row_table_ptr(rowtbl_base, row, rowtbl_big_endian);
            if ((line >= 0x0100u) && (line < 0x3F00u)) {
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
    constexpr int AVDC_CELL_W = 8;
    constexpr int AVDC_CELL_Y_SCALE = 2;
    constexpr int AVDC_GLYPH_ROWS = 11;
    const int avdc_logical_scanlines = std::clamp((int)avdc_.scanlines_per_char_row, 8, 16);
    const int AVDC_CELL_H_LOGICAL = std::max(AVDC_GLYPH_ROWS, std::clamp(avdc_logical_scanlines, 8, 12));
    const int avdc_char_w = AVDC_CELL_W;
    const int avdc_char_h_logical = AVDC_CELL_H_LOGICAL;
    const int avdc_char_h = avdc_char_h_logical * AVDC_CELL_Y_SCALE;
    const int avdc_glyph_top = std::max(0, (avdc_char_h_logical - AVDC_GLYPH_ROWS) / 2);
    const int avdc_y_off = 0;
    const int avdc_cursor_span = std::clamp((int)(avdc_.ir[6] & 0x0F), 1, avdc_logical_scanlines);
    const int avdc_underline_scan = std::clamp((int)(avdc_.ir[7] & 0x0F), 0, avdc_logical_scanlines - 1);
    const bool avdc_blink_enabled = ((avdc_.ir[4] & 0x80u) != 0) || ((avdc_.ir[7] & 0x80u) != 0);
    const uint32_t avdc_blink_period_ticks = ((avdc_.ir[7] & 0x20u) != 0) ? 800000u : 400000u;
    const uint64_t avdc_blink_period_ms = std::max<uint64_t>(1u, avdc_blink_period_ticks / 4000u);
    const uint64_t avdc_blink_phase = (uint64_t)
        (std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch()).count() /
         avdc_blink_period_ms);
    const bool avdc_cursor_blink_on =
        !avdc_blink_enabled || ((avdc_blink_phase & 1u) == 0u);
    const auto avdc_cursor_band = [&]() {
        int start_scan = 0;
        int end_scan = avdc_logical_scanlines - 1;
        if (avdc_cursor_span <= 1) {
            start_scan = avdc_underline_scan;
            end_scan = avdc_underline_scan;
        } else {
            start_scan = std::max(0, avdc_logical_scanlines - avdc_cursor_span);
            end_scan = avdc_logical_scanlines - 1;
        }
        return std::pair<int, int>{start_scan, end_scan};
    };
    const auto [avdc_cursor_scan0, avdc_cursor_scan1] = avdc_cursor_band();
    const auto build_line_bases = [&](bool row_table_variant) {
        std::vector<uint16_t> lines((size_t)avdc_rows, linear_base);
        if (row_table_variant) {
            for (int row = 0; row < avdc_rows; row++) {
                lines[(size_t)row] = read_row_table_ptr(rowtbl_base, row, rowtbl_big_endian);
            }
        } else {
            for (int row = 0; row < avdc_rows; row++) {
                lines[(size_t)row] = (uint16_t)((linear_base + row * avdc_stride) & 0x3FFFu);
            }
        }
        return lines;
    };
    const std::vector<uint16_t> rowtbl_line_bases =
        use_row_table ? build_line_bases(true) : std::vector<uint16_t>();
    const std::vector<uint16_t> linear_line_bases = build_line_bases(false);
    const auto avdc_char_addr = [&](const std::vector<uint16_t>& line_bases, int row, int col) -> uint16_t {
        return (uint16_t)((line_bases[(size_t)row] + col) & 0x3FFFu);
    };
    const auto draw_avdc_char = [&](int col, int row, uint8_t ch) {
        if (ch <= 0x20) return;
        for (int ly = 0; ly < AVDC_GLYPH_ROWS; ly++) {
            const int gry = ly;
            const uint8_t bits = avdc_.glyph_rom[ch][gry];
            for (int px = 0; px < avdc_char_w; px++) {
                if (bits & (uint8_t)(0x80u >> px)) {
                    const int fx = col * avdc_char_w + px;
                    for (int ydup = 0; ydup < AVDC_CELL_Y_SCALE; ydup++) {
                        const int fy = avdc_y_off + row * avdc_char_h + ((avdc_glyph_top + ly) * AVDC_CELL_Y_SCALE) + ydup;
                        int dx = 0, dy = 0;
                        map_full_to_disp(fx, fy, dx, dy);
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
        for (int ly = 0; ly < avdc_char_h_logical; ly++) {
            const int scan = (ly * avdc_logical_scanlines) / avdc_char_h_logical;
            if (scan < avdc_cursor_scan0 || scan > avdc_cursor_scan1) {
                continue;
            }
            for (int px = 0; px < avdc_char_w; px++) {
                const int fx = col * avdc_char_w + px;
                for (int ydup = 0; ydup < AVDC_CELL_Y_SCALE; ydup++) {
                    const int fy = avdc_y_off + row * avdc_char_h + (ly * AVDC_CELL_Y_SCALE) + ydup;
                    int dx = 0, dy = 0;
                    map_full_to_disp(fx, fy, dx, dy);
                    disp.add_pixel(dx, dy, 176);
                }
            }
        }
    };

    int rowtbl_visible_nonspace = 0;
    if (!rowtbl_line_bases.empty()) {
        for (int row = 0; row < avdc_rows; row++) {
            for (int col = 0; col < avdc_cols; col++) {
                if (avdc_.vram[avdc_char_addr(rowtbl_line_bases, row, col)] > 0x20) {
                    rowtbl_visible_nonspace++;
                }
            }
        }
    }
    int linear_visible_nonspace = 0;
    for (int row = 0; row < avdc_rows; row++) {
        for (int col = 0; col < avdc_cols; col++) {
            const uint16_t off = avdc_char_addr(linear_line_bases, row, col);
            if (avdc_.vram[off] > 0x20) {
                linear_visible_nonspace++;
            }
        }
    }
    const bool render_row_table = !rowtbl_line_bases.empty();
    avdc_visible_nonspace = render_row_table ? rowtbl_visible_nonspace
                                             : linear_visible_nonspace;
    const std::vector<uint16_t>& chosen_line_bases =
        render_row_table ? rowtbl_line_bases : linear_line_bases;

    const bool avdc_has_visible_text = (avdc_visible_nonspace > 0);
    const bool use_serial_fallback =
        serial_boot_text_available && !avdc_has_visible_text;

    if (render_trace_enabled()) {
        std::fprintf(stderr,
            "[render-avdc] rowtbl_cfg=%d rowtbl_use=%d rowtbl_base=%04x rowtbl_be=%d rowtbl_bias=%d rowtbl_le=%d rowtbl_be_score=%d rowtbl_stride=%d rowtbl_valid=%d rowtbl_hits=%d linear_base=%04x rows=%d cols=%d stride=%d rowtbl_vis=%d linear_vis=%d chosen_vis=%d any=%d serial_fb=%d\n",
            avdc_.use_row_table ? 1 : 0, render_row_table ? 1 : 0, rowtbl_base, rowtbl_big_endian ? 1 : 0, rowtbl_bias,
            rowtbl_le_score, rowtbl_be_score, rowtbl_stride, rowtbl_valid_lines, rowtbl_stride_hits, linear_base,
            avdc_rows, avdc_cols, avdc_stride,
            rowtbl_visible_nonspace, linear_visible_nonspace, avdc_visible_nonspace,
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
                draw_avdc_char(col, row, (uint8_t)line[col]);
            }
        }
    } else {
        if (avdc_has_visible_text) {
            for (int row = 0; row < avdc_rows; row++) {
                for (int col = 0; col < avdc_cols; col++) {
                    const uint16_t off = avdc_char_addr(chosen_line_bases, row, col);
                    draw_avdc_char(col, row, avdc_.vram[off]);
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

void partner_gdp::key_input(uint8_t ch)
{
    if (ch == '\n') {
        ch = '\r';
    }
    key_fifo_.push_back(ch);
    keyboard_.local_keypress();
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
    case 0x04: // CLS
    case 0x06: // CLS + XY=0
    case 0x07: // CLEAR
        text_col_ = 0;
        text_row_ = 0;
        if (terminal_)
            terminal_->reset();
        break;
    case 0x05: // XY=0
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
    case 0x0B:
        // Draw-right command used heavily in GDP clear-line strings.
        text_col_++;
        if (text_col_ >= text_cols_)
            gdp_newline();
        break;
    default:
        break;
    }
}

void partner_gdp::maybe_auto_boot_floppy()
{
    if (!force_floppy_boot_ || hdc.present)
        return;

    const uint16_t pc = get_current_pc();
    const bool in_rom_bootstrap_context =
        rom_enabled && (cpu.sp >= 0xF000);
    // GDP ROM defaults to hard-disk path around 01E8/01F0 (->02F4).
    // Redirect to floppy boot entry (02FA) when HDD is absent.
    if (in_rom_bootstrap_context &&
        ((pc == 0x01E8) || (pc == 0x01F0) || (pc == 0x02F4)))
    {
        cpu.pc = 0x02FA;
        cpu.wz = 0x02FA;
        auto_floppy_key_sent_ = true;
    }
}

uint8_t partner_gdp::io_read(uint16_t port)
{
    port &= 0xFF;

    const uint8_t pa = gdp_video_pio_.port[Z80PIO_PORT_A].output;
    const bool pio_a_output = (gdp_video_pio_.port[Z80PIO_PORT_A].mode == Z80PIO_MODE_OUTPUT);
    const bool strict_gate = pio_a_output && ((pa & 0x18) != 0);
    const bool ef_enabled = !strict_gate || ((pa & 0x08) != 0);
    const bool avdc_enabled = !strict_gate || ((pa & 0x10) != 0);

    if ((port >= 0x20 && port <= 0x2F))
    {
        if (!ef_enabled)
            return 0xFF;
        io_cnt_.ef_rd++;
        return ef_bus_read(&ef9367_, (uint8_t)port);
    }

    if (port >= 0x30 && port <= 0x33) {
        io_cnt_.pio_rd++;
        return gdp_pio_read(&gdp_video_pio_, (uint8_t)(port - 0x30));
    }

    if (port == 0x36) {
        return sync_bit4_ ? 0x10 : 0x00;
    }

    if (port >= AVDC_BASE_PORT && port <= AVDC_LAST_PORT)
    {
        if (!avdc_enabled)
            return 0xFF;
        if (port == 0x34) return avdc_.char_latch;
        if (port == 0x35) return avdc_.attr_latch;
        if (port == 0x36 || port == 0x37) return 0xFF;
        io_cnt_.avdc_rd++;
        return avdc_bus_read(&avdc_, (uint8_t)port);
    }

    return partner::io_read(port);
}

void partner_gdp::io_write(uint16_t port, uint8_t data)
{
    port &= 0xFF;

    const uint8_t pa = gdp_video_pio_.port[Z80PIO_PORT_A].output;
    const bool pio_a_output = (gdp_video_pio_.port[Z80PIO_PORT_A].mode == Z80PIO_MODE_OUTPUT);
    const bool strict_gate = pio_a_output && ((pa & 0x18) != 0);
    const bool ef_enabled = !strict_gate || ((pa & 0x08) != 0);
    const bool avdc_enabled = !strict_gate || ((pa & 0x10) != 0);

    if (port == EF9367_CMD_PORT)
    {
        if (!ef_enabled)
            return;
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
        if (ef_enabled) {
            io_cnt_.ef_wr++;
            ef_bus_write(&ef9367_, (uint8_t)port, data);
            if (gdp_trace_enabled()) {
                std::fprintf(stderr, "[gdp-ef] pc=%04x cr1<=%02x\n", cpu.pc, data);
            }
        }
        return;
    case EF9367_CR2_PORT:
        if (ef_enabled) {
            io_cnt_.ef_wr++;
            ef_bus_write(&ef9367_, (uint8_t)port, data);
            if (gdp_trace_enabled()) {
                std::fprintf(stderr, "[gdp-ef] pc=%04x cr2<=%02x\n", cpu.pc, data);
            }
        }
        return;
    case EF9367_CHSZ_PORT:
        if (ef_enabled) {
            io_cnt_.ef_wr++;
            ef_bus_write(&ef9367_, (uint8_t)port, data);
            if (gdp_trace_enabled()) {
                std::fprintf(stderr, "[gdp-ef] pc=%04x chsz<=%02x\n", cpu.pc, data);
            }
        }
        return;
    case EF9367_DX_PORT: if (ef_enabled) { io_cnt_.ef_wr++; ef_bus_write(&ef9367_, (uint8_t)port, data); } return;
    case EF9367_DY_PORT: if (ef_enabled) { io_cnt_.ef_wr++; ef_bus_write(&ef9367_, (uint8_t)port, data); } return;
    case EF9367_XH_PORT:
        if (ef_enabled) { io_cnt_.ef_wr++; ef_bus_write(&ef9367_, (uint8_t)port, data); }
        return;
    case EF9367_XL_PORT:
        if (ef_enabled) {
            io_cnt_.ef_wr++;
            ef_bus_write(&ef9367_, (uint8_t)port, data);
            text_col_ = (int)(ef9367_.x / 8u);
            if (text_col_ < 0) text_col_ = 0;
            if (text_col_ >= text_cols_) text_col_ = text_cols_ - 1;
        }
        return;
    case EF9367_YH_PORT:
        if (ef_enabled) { io_cnt_.ef_wr++; ef_bus_write(&ef9367_, (uint8_t)port, data); }
        return;
    case EF9367_YL_PORT:
        if (ef_enabled) {
            io_cnt_.ef_wr++;
            ef_bus_write(&ef9367_, (uint8_t)port, data);
            if (gdp_trace_enabled()) {
                std::fprintf(stderr, "[gdp-ef] pc=%04x y<=%u\n", cpu.pc, ef9367_.y);
            }
            text_row_ = (int)(ef9367_.y / 12u);
            if (text_row_ < 0) text_row_ = 0;
            if (text_row_ >= text_rows_) text_row_ = text_rows_ - 1;
        }
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
        ef9367_.scroll_offset = (int8_t)gdp_scroll_;
        return;
    }

    if (port >= AVDC_BASE_PORT && port <= AVDC_LAST_PORT)
    {
        if (avdc_enabled) {
            io_cnt_.avdc_wr++;
            const uint8_t pidx = (uint8_t)(port & 0x0F);
            avdc_port_wr_cnt_[pidx]++;
            if (port == 0x39)
                avdc_cmd_cnt_[data]++;
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
        }
        return;
    }
    partner::io_write(port, data);
}
