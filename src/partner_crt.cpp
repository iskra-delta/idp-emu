#include "partner_crt.hpp"
#include "gui/display.hpp"
#include "terminal/terminal_emulator.hpp"

partner_crt::partner_crt(terminal_profile profile) : terminal_profile_(profile)
{
    terminal_ = make_terminal_emulator(terminal_profile_);
}

void partner_crt::reset()
{
    partner::reset();
    if (terminal_)
        terminal_->reset();
}

void partner_crt::tick()
{
    partner::tick();

    // Consume bytes written to SIO channel A TX so the ROM sees TX-ready again,
    // and forward those bytes into the active terminal emulator.
    if (z80sio_tx_ready(&sio, Z80SIO_CHANNEL_A))
        return;

    const uint8_t ch = z80sio_tx_data(&sio, Z80SIO_CHANNEL_A);
    if (terminal_)
        terminal_->put_char(ch);
}

void partner_crt::render_to(display &disp)
{
    if (terminal_)
        terminal_->render_to(disp);
    else
        disp.clear();
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
    // CRT text output is consumed in tick() from SIO TX channel A.
    partner::io_write(port, data);
}
