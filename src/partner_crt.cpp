#include "partner_crt.hpp"
#include "gui/display.hpp"
#include "terminal/terminal_emulator.hpp"

namespace {

static void consume_terminal_tx(z80sio_t &chip,
                                int channel,
                                std::string &raw_serial,
                                terminal_emulator *terminal)
{
    if (chip.chn[channel].tx_ready)
        return;

    const uint8_t ch = z80sio_tx_data(&chip, channel);
    if ((ch == '\r') || (ch == '\n') || (ch == '\t') || ((ch >= 0x20) && (ch < 0x7F)))
        raw_serial.push_back((char)ch);
    if (terminal)
        terminal->put_char(ch);
}

static void feed_pending_rx(z80sio_t &chip, int channel, std::deque<uint8_t> &fifo)
{
    if (fifo.empty() || chip.chn[channel].rx_ready)
        return;

    const uint8_t ch = fifo.front();
    const bool was_ready = chip.chn[channel].rx_ready;
    z80sio_rx_data(&chip, channel, ch);
    if (!was_ready && chip.chn[channel].rx_ready)
        fifo.pop_front();
}

static void queue_key_to_channel(z80sio_t &chip, int channel, std::deque<uint8_t> &fifo, uint8_t ch)
{
    const bool was_ready = chip.chn[channel].rx_ready;
    if (!was_ready) {
        z80sio_rx_data(&chip, channel, ch);
        if (!was_ready && chip.chn[channel].rx_ready)
            return;
    }
    fifo.push_back(ch);
}

} // namespace

partner_crt::partner_crt(terminal_profile profile, const std::string &rtc_nvram_path)
    : partner(rtc_nvram_path), terminal_profile_(profile)
{
    set_sio_port_lock(sio_port_id::sio1_a, true, "Internal CRT keyboard/terminal (fixed)");
    set_sio_port_lock(sio_port_id::sio1_b, true, "Internal CRT screen output (fixed)");
    terminal_ = make_terminal_emulator(terminal_profile_);
}

void partner_crt::reset()
{
    partner::reset();
    raw_serial_.clear();
    last_pc_ = 0xFFFF;
    for (auto &fifo : key_fifos_)
        fifo.clear();
    if (terminal_)
        terminal_->reset();
}

void partner_crt::tick()
{
    partner::tick();
    const uint16_t pc = get_current_pc();

    // Stage-1 restarts return to 0x2000 without a hardware reset. On the real
    // text machine the restart redraws a fresh boot screen; clear the emulator
    // terminal surface on that edge so stale BIOS/menu text cannot bleed into
    // the next boot banner.
    if ((pc == stage1_entry_pc_) && (last_pc_ != stage1_entry_pc_)) {
        raw_serial_.clear();
        if (terminal_)
            terminal_->reset();
    }
    last_pc_ = pc;

    // The text-model BIOS may route TERMINAL traffic through any Partner SIO
    // channel, so keep all four channels bidirectional for the on-screen CRT.
    feed_pending_rx(sio, Z80SIO_CHANNEL_A, key_fifos_[0]);
    feed_pending_rx(sio, Z80SIO_CHANNEL_B, key_fifos_[1]);
    feed_pending_rx(sio2, Z80SIO_CHANNEL_A, key_fifos_[2]);
    feed_pending_rx(sio2, Z80SIO_CHANNEL_B, key_fifos_[3]);

    consume_terminal_tx(sio, Z80SIO_CHANNEL_A, raw_serial_, terminal_.get());
    consume_terminal_tx(sio, Z80SIO_CHANNEL_B, raw_serial_, terminal_.get());
    consume_terminal_tx(sio2, Z80SIO_CHANNEL_A, raw_serial_, terminal_.get());
    consume_terminal_tx(sio2, Z80SIO_CHANNEL_B, raw_serial_, terminal_.get());
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

    queue_key_to_channel(sio, Z80SIO_CHANNEL_A, key_fifos_[0], ch);
    queue_key_to_channel(sio, Z80SIO_CHANNEL_B, key_fifos_[1], ch);
    queue_key_to_channel(sio2, Z80SIO_CHANNEL_A, key_fifos_[2], ch);
    queue_key_to_channel(sio2, Z80SIO_CHANNEL_B, key_fifos_[3], ch);
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
    // CRT text output is consumed in tick() from whichever Partner SIO
    // channels the BIOS has routed TERMINAL traffic through.
    partner::io_write(port, data);
}
