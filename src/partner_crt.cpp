#include "partner_crt.hpp"
#include "gui/display.hpp"
#include "terminal/terminal_emulator.hpp"
#include <cstring>

namespace {

static void consume_terminal_tx(z80sio_t &chip,
                                int channel,
                                std::string &raw_serial,
                                terminal_emulator *terminal)
{
    uint8_t ch = 0;
    if (!z80sio_line_take_tx(&chip, channel, &ch))
        return;
    if ((ch == '\r') || (ch == '\n') || (ch == '\t') || ((ch >= 0x20) && (ch < 0x7F)))
        raw_serial.push_back((char)ch);
    if (terminal)
        terminal->put_char(ch);
}

static bool feed_pending_rx(z80sio_t &chip, int channel,
                            std::deque<uint8_t> &fifo, uint64_t tick)
{
    if (fifo.empty() || !z80sio_rx_enabled(&chip, channel))
        return false;
    if (z80sio_line_receive(&chip, channel, fifo.front(), tick,
                            4000000, 153600)) {
        fifo.pop_front();
        return true;
    }
    return false;
}

static void queue_key_to_channel(std::deque<uint8_t> &fifo, uint8_t ch)
{
    fifo.push_back(ch);
}

static bool ends_with(const std::string &text, const char *suffix)
{
    const size_t suffix_size = std::strlen(suffix);
    return text.size() >= suffix_size &&
        text.compare(text.size() - suffix_size, suffix_size, suffix) == 0;
}

} // namespace

partner_crt::partner_crt(terminal_profile profile, const std::string &rtc_nvram_path)
    : partner(rtc_nvram_path), terminal_profile_(profile)
{
    set_sio_port_lock(sio_port_id::sio1_a, true, "Internal CRT keyboard/terminal (fixed)");
    sio_device_config squid;
    squid.kind = sio_device_kind::internal_squid;
    (void)set_sio_device_config(sio_port_id::sio1_b, squid);
    terminal_ = make_terminal_emulator(terminal_profile_);
}

void partner_crt::reset()
{
    partner::reset();
    raw_serial_.clear();
    last_pc_ = 0xFFFF;
    cpm_console_started_ = false;
    key_fifo_.clear();
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
    if (!cpm_console_started_ && (pc == stage1_entry_pc_) &&
        (last_pc_ != stage1_entry_pc_)) {
        raw_serial_.clear();
        if (terminal_)
            terminal_->reset();
    }
    last_pc_ = pc;

    // The CRT model exposes only the fixed internal keyboard/terminal channel
    // on the built-in screen. Other serial ports are attachable peripherals and
    // should not bleed into the on-screen terminal.
    if (feed_pending_rx(sio, Z80SIO_CHANNEL_A, key_fifo_, get_tick_count()))
        request_sio_service();
    consume_terminal_tx(sio, Z80SIO_CHANNEL_A, raw_serial_, terminal_.get());
    if (!cpm_console_started_ && ends_with(raw_serial_, "CP/M V3.0 Loader"))
        cpm_console_started_ = true;
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

    queue_key_to_channel(key_fifo_, ch);
}

bool partner_crt::keyboard_input_ready() const
{
    return z80sio_rx_enabled(&sio, Z80SIO_CHANNEL_A);
}

size_t partner_crt::pending_key_count() const
{
    return key_fifo_.size() +
        (z80sio_line_rx_busy(&sio, Z80SIO_CHANNEL_A) ? 1u : 0u) +
        (z80sio_rx_ready(&sio, Z80SIO_CHANNEL_A) ? 1u : 0u);
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
    // The built-in CRT screen is fed only from the fixed internal SIO channel;
    // other serial ports remain off-screen attachable peripherals.
    partner::io_write(port, data);
}
