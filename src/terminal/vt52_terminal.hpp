#pragma once
#include "terminal_emulator.hpp"
#include <cstdint>

class vt52_terminal : public terminal_emulator
{
public:
    static constexpr int cols = 80;
    static constexpr int rows = 25;

    explicit vt52_terminal(bool start_at_bottom = true);

    void reset() override;
    void put_char(uint8_t ch) override;
    void render_to(display &disp) const override;

private:
    enum class esc_state_t : uint8_t
    {
        none,
        esc,
        cursor_row,
        cursor_col,
        skip_1_param
    };

    uint8_t screen_[cols * rows]{};
    int cursor_col_ = 0;
    int cursor_row_ = 0;
    int saved_col_ = 0;
    int saved_row_ = 0;
    bool cursor_visible_ = true;
    bool wrap_enabled_ = true;
    bool graphics_mode_ = false;
    bool keypad_application_mode_ = false;
    esc_state_t esc_state_ = esc_state_t::none;
    uint8_t esc_row_ = 0;
    bool start_at_bottom_ = true;

    void clear_screen();
    void scroll_up();
    void scroll_down();
    void newline();
    void reverse_newline();
    void erase_eol();
    void erase_eos();
    void erase_bos();
    void insert_line();
    void delete_line();
    void insert_char();
    void delete_char();
    void move_cursor_rel(int drow, int dcol);
    void set_cursor_abs(int row, int col);
};
