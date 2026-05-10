#include "partner_crt.hpp"
#include "gui/display.hpp"
#include "terminal/terminal_emulator.hpp"

partner_crt::partner_crt(terminal_profile profile) : terminal_profile_(profile)
{
    set_sio_port_lock(sio_port_id::sio1_a, true, "Internal CRT terminal (fixed)");
    terminal_ = make_terminal_emulator(terminal_profile_);
}

void partner_crt::reset()
{
    partner::reset();
    raw_serial_.clear();
    key_fifo_.clear();
    if (terminal_)
        terminal_->reset();
}

void partner_crt::tick()
{
    partner::tick();

    if (!key_fifo_.empty() && !sio.chn[Z80SIO_CHANNEL_A].rx_ready)
    {
        const uint8_t ch = key_fifo_.front();
        const bool was_ready = sio.chn[Z80SIO_CHANNEL_A].rx_ready;
        z80sio_rx_data(&sio, Z80SIO_CHANNEL_A, ch);
        if (!was_ready && sio.chn[Z80SIO_CHANNEL_A].rx_ready)
            key_fifo_.pop_front();
    }

    // Consume bytes written to SIO channel A TX so the ROM sees TX-ready again,
    // and forward those bytes into the active terminal emulator.
    if (sio.chn[Z80SIO_CHANNEL_A].tx_ready)
        return;

    const uint8_t ch = z80sio_tx_data(&sio, Z80SIO_CHANNEL_A);
    if ((ch == '\r') || (ch == '\n') || (ch == '\t') || ((ch >= 0x20) && (ch < 0x7F)))
        raw_serial_.push_back((char)ch);
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
    if (ch == '\n')
        ch = '\r';

    const bool was_ready = sio.chn[Z80SIO_CHANNEL_A].rx_ready;
    if (!was_ready) {
        z80sio_rx_data(&sio, Z80SIO_CHANNEL_A, ch);
        if (!was_ready && sio.chn[Z80SIO_CHANNEL_A].rx_ready)
            return;
    }

    key_fifo_.push_back(ch);
}

std::string partner_crt::dump_terminal_text() const
{
    return terminal_ ? terminal_->dump_text() : std::string{};
}

std::string partner_crt::dump_raw_serial_text() const
{
    return raw_serial_;
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
