#include "vt52_terminal.hpp"
#include "../gui/display.hpp"
#include <cstring>

vt52_terminal::vt52_terminal(bool start_at_bottom) : start_at_bottom_(start_at_bottom)
{
    reset();
}

void vt52_terminal::reset()
{
    memset(screen_, ' ', sizeof(screen_));
    cursor_col_ = 0;
    cursor_row_ = start_at_bottom_ ? (rows - 1) : 0;
    saved_col_ = cursor_col_;
    saved_row_ = cursor_row_;
    cursor_visible_ = true;
    wrap_enabled_ = true;
    graphics_mode_ = false;
    keypad_application_mode_ = false;
    esc_state_ = esc_state_t::none;
    esc_row_ = 0;
}

void vt52_terminal::set_cursor_abs(int row, int col)
{
    if (row < 0) row = 0;
    if (row >= rows) row = rows - 1;
    if (col < 0) col = 0;
    if (col >= cols) col = cols - 1;
    cursor_row_ = row;
    cursor_col_ = col;
}

void vt52_terminal::move_cursor_rel(int drow, int dcol)
{
    set_cursor_abs(cursor_row_ + drow, cursor_col_ + dcol);
}

void vt52_terminal::newline()
{
    cursor_col_ = 0;
    cursor_row_++;
    if (cursor_row_ >= rows)
    {
        scroll_up();
        cursor_row_ = rows - 1;
    }
}

void vt52_terminal::reverse_newline()
{
    if (cursor_row_ == 0)
    {
        scroll_down();
    }
    else
    {
        cursor_row_--;
    }
}

void vt52_terminal::erase_eol()
{
    uint8_t *line = &screen_[cursor_row_ * cols];
    memset(line + cursor_col_, ' ', cols - cursor_col_);
}

void vt52_terminal::erase_eos()
{
    erase_eol();
    for (int r = cursor_row_ + 1; r < rows; r++)
    {
        memset(&screen_[r * cols], ' ', cols);
    }
}

void vt52_terminal::erase_bos()
{
    for (int r = 0; r < cursor_row_; r++)
    {
        memset(&screen_[r * cols], ' ', cols);
    }
    uint8_t *line = &screen_[cursor_row_ * cols];
    memset(line, ' ', cursor_col_ + 1);
}

void vt52_terminal::insert_line()
{
    const size_t tail_rows = (size_t)(rows - 1 - cursor_row_);
    if (tail_rows > 0)
    {
        memmove(&screen_[(cursor_row_ + 1) * cols], &screen_[cursor_row_ * cols], tail_rows * cols);
    }
    memset(&screen_[cursor_row_ * cols], ' ', cols);
}

void vt52_terminal::delete_line()
{
    const size_t tail_rows = (size_t)(rows - 1 - cursor_row_);
    if (tail_rows > 0)
    {
        memmove(&screen_[cursor_row_ * cols], &screen_[(cursor_row_ + 1) * cols], tail_rows * cols);
    }
    memset(&screen_[(rows - 1) * cols], ' ', cols);
}

void vt52_terminal::insert_char()
{
    uint8_t *line = &screen_[cursor_row_ * cols];
    if (cursor_col_ < cols - 1)
    {
        memmove(line + cursor_col_ + 1, line + cursor_col_, cols - cursor_col_ - 1);
    }
    line[cursor_col_] = ' ';
}

void vt52_terminal::delete_char()
{
    uint8_t *line = &screen_[cursor_row_ * cols];
    if (cursor_col_ < cols - 1)
    {
        memmove(line + cursor_col_, line + cursor_col_ + 1, cols - cursor_col_ - 1);
    }
    line[cols - 1] = ' ';
}

void vt52_terminal::clear_screen()
{
    memset(screen_, ' ', sizeof(screen_));
    cursor_col_ = 0;
    cursor_row_ = start_at_bottom_ ? (rows - 1) : 0;
}

void vt52_terminal::scroll_up()
{
    memmove(screen_, screen_ + cols, cols * (rows - 1));
    memset(screen_ + cols * (rows - 1), ' ', cols);
}

void vt52_terminal::scroll_down()
{
    memmove(screen_ + cols, screen_, cols * (rows - 1));
    memset(screen_, ' ', cols);
}

void vt52_terminal::put_char(uint8_t ch)
{
    if (esc_state_ == esc_state_t::cursor_row)
    {
        esc_row_ = ch;
        esc_state_ = esc_state_t::cursor_col;
        return;
    }
    if (esc_state_ == esc_state_t::cursor_col)
    {
        set_cursor_abs((int)esc_row_ - 32, (int)ch - 32);
        esc_state_ = esc_state_t::none;
        return;
    }
    if (esc_state_ == esc_state_t::skip_1_param)
    {
        esc_state_ = esc_state_t::none;
        return;
    }
    if (esc_state_ == esc_state_t::esc)
    {
        esc_state_ = esc_state_t::none;
        switch (ch)
        {
        case 'A': move_cursor_rel(-1, 0); break;
        case 'B': move_cursor_rel(1, 0); break;
        case 'C': move_cursor_rel(0, 1); break;
        case 'D': move_cursor_rel(0, -1); break;
        case 'E': clear_screen(); set_cursor_abs(0, 0); break;
        case 'F': graphics_mode_ = true; break;
        case 'G': graphics_mode_ = false; break;
        case 'H': set_cursor_abs(0, 0); break;
        case 'I': reverse_newline(); break;
        case 'J': erase_eos(); break;
        case 'K': erase_eol(); break;
        case 'L': insert_line(); break;
        case 'M': delete_line(); break;
        case 'N': delete_char(); break;
        case 'O': insert_char(); break;
        case 'Y': esc_state_ = esc_state_t::cursor_row; break;
        case 'Z': break; // ID request is handled by host/device layer if needed.
        case '=': keypad_application_mode_ = true; break;
        case '>': keypad_application_mode_ = false; break;
        case '<': break;
        case 'b':
        case 'c':
            esc_state_ = esc_state_t::skip_1_param;
            break;
        case 'd': erase_bos(); break;
        case 'e': cursor_visible_ = true; break;
        case 'f': cursor_visible_ = false; break;
        case 'j': saved_row_ = cursor_row_; saved_col_ = cursor_col_; break;
        case 'k': set_cursor_abs(saved_row_, saved_col_); break;
        case 'p': break;
        case 'q': break;
        case 'v': wrap_enabled_ = true; break;
        case 'w': wrap_enabled_ = false; break;
        default: break;
        }
        return;
    }

    switch (ch)
    {
    case 0x1B:
        esc_state_ = esc_state_t::esc;
        break;
    case 0x0D:
        cursor_col_ = 0;
        break;
    case 0x0A:
    case 0x0B:
        newline();
        break;
    case 0x0C:
    case 0x1C:
        clear_screen();
        break;
    case 0x08:
        if (cursor_col_ > 0) cursor_col_--;
        break;
    case 0x09:
        cursor_col_ = (cursor_col_ + 8) & ~7;
        if (cursor_col_ >= cols)
        {
            if (wrap_enabled_) newline();
            else cursor_col_ = cols - 1;
        }
        break;
    case 0x00:
    case 0x07:
    case 0x11:
    case 0x13:
        break;
    default:
        if (ch >= 32 && ch < 127)
        {
            screen_[cursor_row_ * cols + cursor_col_] = ch;
            cursor_col_++;
            if (cursor_col_ >= cols)
            {
                if (wrap_enabled_) newline();
                else cursor_col_ = cols - 1;
            }
        }
        break;
    }
}

void vt52_terminal::render_to(display &disp) const
{
    disp.clear();
    for (int row = 0; row < rows; row++)
    {
        for (int col = 0; col < cols; col++)
        {
            const uint8_t ch = screen_[row * cols + col];
            if (ch > 32 && ch < 127)
                disp.draw_char(col, row, (char)ch);
        }
    }
    if (cursor_visible_)
        disp.draw_char(cursor_col_, cursor_row_, '_');
}
