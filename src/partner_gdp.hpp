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
    explicit partner_gdp(terminal_profile profile = terminal_profile::vt100_ansi);

    void reset() override;
    void tick() override;
    void render_to(display &disp);
    void key_input(uint8_t ch);
    std::string dump_terminal_text() const;
    std::string dump_raw_serial_text() const;
    const scn2674_t& get_avdc() const { return avdc_; }
    const ef9367_t& get_ef9367() const { return ef9367_; }
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
    uint8_t get_gdp_pio_port_a() const { return gdp_video_pio_.port[Z80PIO_PORT_A].output; }

protected:
    uint8_t io_read(uint16_t port) override;
    void io_write(uint16_t port, uint8_t data) override;

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
    uint16_t sync_div_ = 0;
    bool sync_bit4_ = false;
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
    uint16_t bios_key_write_ptr_ = 0;
    partner_gdp_keyboard keyboard_{};
    void sync_ef_mode_from_gdp_pio();
    bool late_bios_queue_active() const;
    bool enqueue_late_bios_key(uint8_t ch);

    void gdp_put_char(uint8_t ch);
    void gdp_newline();
    void gdp_command(uint8_t cmd);
    void maybe_auto_boot_floppy();
};
