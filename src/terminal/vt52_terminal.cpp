#include "vt52_terminal.hpp"
#include "../gui/display.hpp"
#include <chrono>
#include <cstring>
#include <string>

vt52_terminal::vt52_terminal(bool start_at_bottom) : start_at_bottom_(start_at_bottom)
{
    reset();
}

void vt52_terminal::reset()
{
    memset(screen_, ' ', sizeof(screen_));
    memset(attr_, 0, sizeof(attr_));
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
    current_attr_ = 0;
    reset_csi();
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
    uint8_t *attr_line = &attr_[cursor_row_ * cols];
    memset(line + cursor_col_, ' ', cols - cursor_col_);
    memset(attr_line + cursor_col_, 0, cols - cursor_col_);
}

void vt52_terminal::erase_eos()
{
    erase_eol();
    for (int r = cursor_row_ + 1; r < rows; r++)
    {
        memset(&screen_[r * cols], ' ', cols);
        memset(&attr_[r * cols], 0, cols);
    }
}

void vt52_terminal::erase_bos()
{
    for (int r = 0; r < cursor_row_; r++)
    {
        memset(&screen_[r * cols], ' ', cols);
        memset(&attr_[r * cols], 0, cols);
    }
    uint8_t *line = &screen_[cursor_row_ * cols];
    uint8_t *attr_line = &attr_[cursor_row_ * cols];
    memset(line, ' ', cursor_col_ + 1);
    memset(attr_line, 0, cursor_col_ + 1);
}

void vt52_terminal::insert_line()
{
    const size_t tail_rows = (size_t)(rows - 1 - cursor_row_);
    if (tail_rows > 0)
    {
        memmove(&screen_[(cursor_row_ + 1) * cols], &screen_[cursor_row_ * cols], tail_rows * cols);
        memmove(&attr_[(cursor_row_ + 1) * cols], &attr_[cursor_row_ * cols], tail_rows * cols);
    }
    memset(&screen_[cursor_row_ * cols], ' ', cols);
    memset(&attr_[cursor_row_ * cols], 0, cols);
}

void vt52_terminal::delete_line()
{
    const size_t tail_rows = (size_t)(rows - 1 - cursor_row_);
    if (tail_rows > 0)
    {
        memmove(&screen_[cursor_row_ * cols], &screen_[(cursor_row_ + 1) * cols], tail_rows * cols);
        memmove(&attr_[cursor_row_ * cols], &attr_[(cursor_row_ + 1) * cols], tail_rows * cols);
    }
    memset(&screen_[(rows - 1) * cols], ' ', cols);
    memset(&attr_[(rows - 1) * cols], 0, cols);
}

void vt52_terminal::insert_char()
{
    uint8_t *line = &screen_[cursor_row_ * cols];
    if (cursor_col_ < cols - 1)
    {
        memmove(line + cursor_col_ + 1, line + cursor_col_, cols - cursor_col_ - 1);
        uint8_t *attr_line = &attr_[cursor_row_ * cols];
        memmove(attr_line + cursor_col_ + 1, attr_line + cursor_col_, cols - cursor_col_ - 1);
    }
    line[cursor_col_] = ' ';
    attr_[cursor_row_ * cols + cursor_col_] = 0;
}

void vt52_terminal::delete_char()
{
    uint8_t *line = &screen_[cursor_row_ * cols];
    if (cursor_col_ < cols - 1)
    {
        memmove(line + cursor_col_, line + cursor_col_ + 1, cols - cursor_col_ - 1);
        uint8_t *attr_line = &attr_[cursor_row_ * cols];
        memmove(attr_line + cursor_col_, attr_line + cursor_col_ + 1, cols - cursor_col_ - 1);
    }
    line[cols - 1] = ' ';
    attr_[cursor_row_ * cols + (cols - 1)] = 0;
}

void vt52_terminal::clear_screen()
{
    memset(screen_, ' ', sizeof(screen_));
    memset(attr_, 0, sizeof(attr_));
    cursor_col_ = 0;
    cursor_row_ = 0;
    current_attr_ = 0;
    esc_state_ = esc_state_t::none;
    reset_csi();
}

void vt52_terminal::scroll_up()
{
    memmove(screen_, screen_ + cols, cols * (rows - 1));
    memmove(attr_, attr_ + cols, cols * (rows - 1));
    memset(screen_ + cols * (rows - 1), ' ', cols);
    memset(attr_ + cols * (rows - 1), 0, cols);
}

void vt52_terminal::scroll_down()
{
    memmove(screen_ + cols, screen_, cols * (rows - 1));
    memmove(attr_ + cols, attr_, cols * (rows - 1));
    memset(screen_, ' ', cols);
    memset(attr_, 0, cols);
}

void vt52_terminal::reset_csi()
{
    memset(csi_params_, 0, sizeof(csi_params_));
    csi_param_count_ = 1;
    csi_private_ = false;
}

void vt52_terminal::handle_csi(uint8_t final_char)
{
    const int p0 = csi_params_[0];
    const int p1 = csi_params_[1];
    switch (final_char)
    {
    case 'A':
        move_cursor_rel(-((p0 > 0) ? p0 : 1), 0);
        break;
    case 'B':
        move_cursor_rel(((p0 > 0) ? p0 : 1), 0);
        break;
    case 'C':
        move_cursor_rel(0, ((p0 > 0) ? p0 : 1));
        break;
    case 'D':
        move_cursor_rel(0, -((p0 > 0) ? p0 : 1));
        break;
    case 'H':
    case 'f':
        set_cursor_abs(((p0 > 0) ? p0 : 1) - 1, ((p1 > 0) ? p1 : 1) - 1);
        break;
    case 'J':
        switch (p0)
        {
        case 0: erase_eos(); break;
        case 1: erase_bos(); break;
        case 2:
        case 3: clear_screen(); break;
        default: break;
        }
        break;
    case 'K':
        switch (p0)
        {
        case 0: erase_eol(); break;
        case 1: erase_bos(); break;
        case 2:
        {
            const int keep_col = cursor_col_;
            cursor_col_ = 0;
            erase_eol();
            cursor_col_ = keep_col;
            break;
        }
        default: break;
        }
        break;
    case 'h':
        if (csi_private_ && p0 == 25) {
            cursor_visible_ = true;
        }
        break;
    case 'l':
        if (csi_private_ && p0 == 25) {
            cursor_visible_ = false;
        }
        break;
    case 'm':
        if (csi_param_count_ <= 0) {
            current_attr_ = 0;
            break;
        }
        for (int i = 0; i < csi_param_count_; ++i) {
            const int param = csi_params_[i];
            switch (param)
            {
            case 0:
                current_attr_ = 0;
                break;
            case 1:
                current_attr_ |= attr_highlight;
                break;
            case 4:
                current_attr_ |= attr_underline;
                break;
            case 7:
                current_attr_ |= attr_inverse;
                break;
            case 22:
                current_attr_ &= (uint8_t)~attr_highlight;
                break;
            case 24:
                current_attr_ &= (uint8_t)~attr_underline;
                break;
            case 27:
                current_attr_ &= (uint8_t)~attr_inverse;
                break;
            default:
                break;
            }
        }
        break;
    default:
        break;
    }
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
    if (esc_state_ == esc_state_t::attr_param)
    {
        current_attr_ = (ch == '1') ? attr_highlight : 0;
        esc_state_ = esc_state_t::none;
        return;
    }
    if (esc_state_ == esc_state_t::csi)
    {
        if (ch >= '0' && ch <= '9')
        {
            if (csi_param_count_ < 1)
                csi_param_count_ = 1;
            csi_params_[csi_param_count_ - 1] =
                csi_params_[csi_param_count_ - 1] * 10 + (int)(ch - '0');
            return;
        }
        if (ch == ';')
        {
            if (csi_param_count_ < 4)
                csi_param_count_++;
            return;
        }
        if (ch == '?' && csi_param_count_ == 1 && csi_params_[0] == 0)
        {
            csi_private_ = true;
            return;
        }
        handle_csi(ch);
        esc_state_ = esc_state_t::none;
        reset_csi();
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
        case 'E': clear_screen(); break;
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
        case '[':
            reset_csi();
            esc_state_ = esc_state_t::csi;
            break;
        case 'b':
            esc_state_ = esc_state_t::attr_param;
            break;
        case 'c':
            esc_state_ = esc_state_t::skip_1_param;
            break;
        case 'd': erase_bos(); break;
        case 'e': cursor_visible_ = true; break;
        case 'f': cursor_visible_ = false; break;
        case 'j': saved_row_ = cursor_row_; saved_col_ = cursor_col_; break;
        case 'k': set_cursor_abs(saved_row_, saved_col_); break;
        case 'p': current_attr_ = attr_inverse; break;
        case 'q': current_attr_ = 0; break;
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
            const int off = cursor_row_ * cols + cursor_col_;
            screen_[off] = ch;
            attr_[off] = (uint8_t)current_attr_;
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
    constexpr uint8_t MONO_BLACK = 0;
    constexpr uint8_t MONO_STD = 168;
    constexpr uint8_t MONO_HI = 232;
    const auto mode = disp.get_phosphor_type();
    const bool curved =
        (mode == display::phosphor_type::green) ||
        (mode == display::phosphor_type::orange) ||
        (mode == display::phosphor_type::retro_cool);
    const int margin_x = curved ? 24 : 0;
    const int margin_y = curved ? 12 : 0;

    disp.set_content_origin(margin_x, margin_y);
    disp.set_content_area(cols * display::CHAR_W + margin_x * 2,
                          rows * display::CHAR_H + margin_y * 2);
    disp.set_preserve_aspect(false);
    disp.clear_all();
    for (int row = 0; row < rows; row++)
    {
        for (int col = 0; col < cols; col++)
        {
            const int off = row * cols + col;
            const uint8_t ch = screen_[off];
            const uint8_t attr = attr_[off];
            const bool underline = (attr & attr_underline) != 0;
            const bool highlight = (attr & attr_highlight) != 0;
            const bool inverse = (attr & attr_inverse) != 0;

            uint8_t fg = highlight ? MONO_HI : MONO_STD;
            uint8_t bg = MONO_BLACK;
            if (inverse) {
                fg = MONO_BLACK;
                bg = highlight ? MONO_HI : MONO_STD;
            }

            if (ch > 32 && ch < 127) {
                disp.draw_char(col, row, (char)ch, fg, bg);
            } else if (bg != MONO_BLACK) {
                disp.fill_char_cell(col, row, bg);
            }
            if (underline) {
                const int x0 = margin_x + col * display::CHAR_W;
                const int y = margin_y + row * display::CHAR_H + (display::CHAR_H - 2);
                for (int x = x0; x < x0 + display::CHAR_W - 1; ++x) {
                    disp.set_level_pixel(x, y, fg);
                }
            }
        }
    }
    if (cursor_visible_)
    {
        using namespace std::chrono;
        const auto now = steady_clock::now().time_since_epoch();
        const bool blink_on = ((duration_cast<milliseconds>(now).count() / 500) % 2) == 0;
        if (blink_on)
            disp.fill_char_cell(cursor_col_, cursor_row_, MONO_HI);
    }
}

std::string vt52_terminal::dump_text() const
{
    std::string out;
    out.reserve(rows * (cols + 1));
    for (int row = 0; row < rows; row++)
    {
        const char *line = reinterpret_cast<const char *>(&screen_[row * cols]);
        int end = cols;
        while (end > 0 && line[end - 1] == ' ')
            end--;
        out.append(line, line + end);
        out.push_back('\n');
    }
    return out;
}
