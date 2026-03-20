#pragma once
#include "partner.hpp"
#include "terminal/terminal_emulator.hpp"
#include "terminal/terminal_factory.hpp"
#include <memory>

class display;

class partner_crt : public partner
{
public:
    static constexpr int TERM_COLS = 80;
    static constexpr int TERM_ROWS = 25;

    explicit partner_crt(terminal_profile profile = terminal_profile::vt52);

    void reset() override;
    void tick() override;
    void render_to(display &disp);
    void key_input(uint8_t ch);
    std::string dump_terminal_text() const;
    std::string dump_raw_serial_text() const;

protected:
    uint8_t io_read(uint16_t port) override;
    void io_write(uint16_t port, uint8_t data) override;

private:
    terminal_profile terminal_profile_ = terminal_profile::vt52;
    std::unique_ptr<terminal_emulator> terminal_;
    std::string raw_serial_;
    void maybe_auto_boot_floppy();
};
