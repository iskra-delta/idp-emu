#pragma once
#include "terminal_emulator.hpp"
#include <cstdint>

class vt52_terminal : public terminal_emulator
{
public:
    static constexpr int cols = 80;
    static constexpr int rows = 24;

    explicit vt52_terminal(bool start_at_bottom = true);

    void reset() override;
    void put_char(uint8_t ch) override;
    void render_to(display &disp) const override;
    std::string dump_text() const override;

private:
    enum class esc_state_t : uint8_t
    {
        none,
        esc,
        cursor_row,
        cursor_col,
        csi,
        attr_param,
        skip_1_param
    };

    static constexpr uint8_t attr_highlight = 0x10;
    static constexpr uint8_t attr_inverse = 0x20;

    uint8_t screen_[cols * rows]{};
    uint8_t attr_[cols * rows]{};
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
    uint8_t current_attr_ = 0;
    int csi_params_[4]{};
    int csi_param_count_ = 0;
    bool csi_private_ = false;

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
    void reset_csi();
    void handle_csi(uint8_t final_char);
};
