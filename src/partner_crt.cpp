#include "partner_crt.hpp"
#include "gui/display.hpp"

void partner_crt::put_char(uint8_t ch)
{
    switch (ch)
    {
    case 0x0D: // CR
        cursor_col_ = 0;
        break;
    case 0x0A: // LF
        cursor_row_++;
        if (cursor_row_ >= TERM_ROWS)
        {
            scroll_up();
            cursor_row_ = TERM_ROWS - 1;
        }
        break;
    case 0x08: // BS
        if (cursor_col_ > 0)
            cursor_col_--;
        break;
    case 0x07: // BEL
        break;
    default:
        if (ch >= 32 && ch < 127)
        {
            screen_[cursor_row_ * TERM_COLS + cursor_col_] = ch;
            cursor_col_++;
            if (cursor_col_ >= TERM_COLS)
            {
                cursor_col_ = 0;
                cursor_row_++;
                if (cursor_row_ >= TERM_ROWS)
                {
                    scroll_up();
                    cursor_row_ = TERM_ROWS - 1;
                }
            }
        }
        break;
    }
}

void partner_crt::scroll_up()
{
    memmove(screen_, screen_ + TERM_COLS, TERM_COLS * (TERM_ROWS - 1));
    memset(screen_ + TERM_COLS * (TERM_ROWS - 1), ' ', TERM_COLS);
}

void partner_crt::render_to(display &disp)
{
    disp.clear();
    for (int row = 0; row < TERM_ROWS; row++)
    {
        for (int col = 0; col < TERM_COLS; col++)
        {
            uint8_t ch = screen_[row * TERM_COLS + col];
            if (ch > 32 && ch < 127)
                disp.draw_char(col, row, (char)ch);
        }
    }

    // Draw cursor as a block
    disp.draw_char(cursor_col_, cursor_row_, '_');
}

void partner_crt::key_input(uint8_t ch)
{
    bool was_ready = sio.chn[0].rx_ready;
    z80sio_rx_data(&sio, 0, ch);

    // Debugger/host injection fallback:
    // if channel A RX is not enabled yet, force one byte into RX so ROM
    // polling on SIO status can still observe incoming input.
    if (!sio.chn[0].rx_ready && !was_ready)
    {
        sio.chn[0].rx_data = ch;
        sio.chn[0].rx_ready = true;
        sio.chn[0].int_state |= Z80SIO_INT_NEEDED;
    }
}

uint8_t partner_crt::io_read(uint16_t port)
{
    return partner::io_read(port);
}

void partner_crt::io_write(uint16_t port, uint8_t data)
{
    port &= 0xFF;

    // Intercept SIO channel 0 data writes for terminal output
    if (port == 0xD8)
        put_char(data);

    // Let base class handle the actual chip I/O
    partner::io_write(port, data);
}
