#pragma once
#include "partner.hpp"
#include "partner_gdp_keyboard.hpp"
#include "scn2674.h"
#include "ef9367.h"
#include "z80pio.h"
#include "terminal/terminal_emulator.hpp"
#include "terminal/terminal_factory.hpp"
#include <memory>
#include <string>
#include <array>
#include <deque>

class display;

class partner_gdp : public partner
{
public:
    static constexpr size_t KEY_FIFO_CAPACITY = 64;

    explicit partner_gdp(terminal_profile profile = terminal_profile::vt100_ansi,
                         const std::string &rtc_nvram_path = "partner_cmos.bin");

    void reset() override;
    void tick() override;
    void render_to(display &disp);
    bool key_input(uint8_t ch);
    bool keyboard_input_ready() const;
    size_t pending_key_count() const;
    std::string dump_terminal_text() const;
    std::string dump_raw_serial_text() const;
    const scn2674_t& get_avdc() const { return avdc_; }
    const ef9367_t& get_ef9367() const { return ef9367_; }
    const z80pio_t& get_gdp_pio() const { return gdp_video_pio_; }
    partner_gdp_keyboard& get_keyboard() { return keyboard_; }
    const partner_gdp_keyboard& get_keyboard() const { return keyboard_; }
    struct io_counters {
        uint64_t ef_rd = 0;
        uint64_t ef_wr = 0;
        uint64_t avdc_rd = 0;
        uint64_t avdc_wr = 0;
        uint64_t pio_rd = 0;
        uint64_t pio_wr = 0;
    };
    const io_counters& get_io_counters() const { return io_cnt_; }
    const std::array<uint64_t, 16>& get_avdc_port_writes() const { return avdc_port_wr_cnt_; }
    const std::array<uint64_t, 256>& get_avdc_cmd_writes() const { return avdc_cmd_cnt_; }
    uint64_t get_avdc_char_writes() const { return avdc_char_wr_cnt_; }
    uint64_t get_avdc_char_nonspace_writes() const { return avdc_char_nonspace_wr_cnt_; }
    const std::array<uint64_t, 256>& get_avdc_char_hist() const { return avdc_char_hist_; }
    uint8_t get_gdp_scroll() const { return gdp_scroll_; }
    uint8_t get_gdp_pio_port_a() const { return Z80PIO_GET_PA(gdp_video_pio_pins_); }
    uint8_t get_gdp_pio_port_b() const { return Z80PIO_GET_PB(gdp_video_pio_pins_); }

protected:
    uint8_t io_read(uint16_t port) override;
    void io_write(uint16_t port, uint8_t data) override;
    bool get_ctc3_trigger_edge() const override { return avdc_irq_edge_; }
    uint64_t clock_expansion_daisy_chain(uint64_t bus_pins) override;

private:
    terminal_profile terminal_profile_ = terminal_profile::vt100_ansi;
    std::unique_ptr<terminal_emulator> terminal_;
    std::string raw_serial_;

    ef9367_t ef9367_{};
    scn2674_t avdc_{};
    z80pio_t gdp_video_pio_{}; // GDP-board local control PIO exposed at 0x30..0x33

    int text_col_ = 0;
    int text_row_ = 0;
    int text_cols_ = 132;
    int text_rows_ = 26;
    bool expect_abs_x_ = false;
    bool expect_abs_y_ = false;
    io_counters io_cnt_{};
    std::array<uint64_t, 16> avdc_port_wr_cnt_{};
    std::array<uint64_t, 256> avdc_cmd_cnt_{};
    uint64_t avdc_char_wr_cnt_ = 0;
    uint64_t avdc_char_nonspace_wr_cnt_ = 0;
    std::array<uint64_t, 256> avdc_char_hist_{};
    uint8_t gdp_scroll_ = 0; // external GDP scroll latch (port 0x36 write)
    bool avdc_takeover_ = false;
    uint16_t avdc_rowtbl_base_cache_ = 0;
    bool avdc_rowtbl_base_cache_valid_ = false;
    bool avdc_rowtbl_be_cache_ = false;
    int avdc_rowtbl_bias_cache_ = 0;
    std::deque<uint8_t> key_fifo_{};
    partner_gdp_keyboard keyboard_{};
    uint64_t gdp_video_pio_pins_ = 0;
    uint64_t ef9367_pins_ = 0;
    uint64_t avdc_pins_ = 0;
    bool avdc_restrict_ = false; // DADD13/LL latched at falling BLANK
    bool gdp_pixel_latch_ = true; // active-low pixel sense returned on 36h D7

    // ST8 exposes the PAL-conditioned active-low AVDC interrupt, not raw VB.
    bool avdc_irq_edge_ = false;

    void gdp_put_char(uint8_t ch);
    void gdp_newline();
    void gdp_command(uint8_t cmd);
};
