#pragma once
#include "partner.hpp"
#include <cstring>

class display;

class partner_crt : public partner
{
public:
    static constexpr int TERM_COLS = 80;
    static constexpr int TERM_ROWS = 25;

    partner_crt() { memset(screen_, ' ', sizeof(screen_)); }

    void put_char(uint8_t ch);
    void scroll_up();
    void render_to(display &disp);
    void key_input(uint8_t ch);

protected:
    uint8_t io_read(uint16_t port) override;
    void io_write(uint16_t port, uint8_t data) override;

private:
    uint8_t screen_[TERM_COLS * TERM_ROWS]{};
    int cursor_col_ = 0;
    int cursor_row_ = 0;
};
