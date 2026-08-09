#pragma once
#include "partner.hpp"
#include "terminal/terminal_emulator.hpp"
#include "terminal/terminal_factory.hpp"
#include <array>
#include <deque>
#include <memory>

class display;

class partner_crt : public partner
{
public:
    static constexpr int TERM_COLS = 80;
    static constexpr int TERM_ROWS = 24;

    explicit partner_crt(terminal_profile profile = terminal_profile::vt52,
                         const std::string &rtc_nvram_path = "partner_cmos.bin");

    void reset() override;
    void tick() override;
    void render_to(display &disp);
    void key_input(uint8_t ch);
    bool keyboard_input_ready() const;
    size_t pending_key_count() const;
    std::string dump_terminal_text() const;
    std::string dump_raw_serial_text() const;

protected:
    uint8_t io_read(uint16_t port) override;
    void io_write(uint16_t port, uint8_t data) override;

private:
    static constexpr uint16_t stage1_entry_pc_ = 0x2000;
    terminal_profile terminal_profile_ = terminal_profile::vt52;
    std::unique_ptr<terminal_emulator> terminal_;
    std::string raw_serial_;
    std::deque<uint8_t> key_fifo_;
    uint16_t last_pc_ = 0xFFFF;
};
