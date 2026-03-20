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
    raw_serial_.clear();
    if (terminal_)
        terminal_->reset();
}

void partner_crt::tick()
{
    partner::tick();
    maybe_auto_boot_floppy();

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
    // If the previous floppy attempt stranded the 8272 in RESULT without a
    // live IRQ, start the next explicit boot command from a clean controller
    // state instead of reusing stale result bytes.
    if ((ch == 'F') || (ch == 'f') || (ch == 'A') || (ch == 'a'))
    {
        if ((fdc.phase == I8272_PHASE_RESULT) && !fdc.irq_request && !fdc.int_pending)
        {
            fdc.phase = I8272_PHASE_IDLE;
            fdc.cmd_idx = 0;
            fdc.cmd_len = 0;
            fdc.cmd_code = 0;
            fdc.result_idx = 0;
            fdc.result_len = 0;
            fdc.irq_delay = 0;
            fdc.irq_sets_sense = false;
            fdc.irq_enters_result = false;
            fdc.msr = I8272_MSR_RQM;
        }
    }

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

void partner_crt::maybe_auto_boot_floppy()
{
    if (!force_floppy_boot_ || hdc.present)
        return;

    const uint16_t pc = get_current_pc();
    const bool in_rom_bootstrap_context =
        rom_enabled && (cpu.sp >= 0xF000);
    if (in_rom_bootstrap_context && ((pc == 0x017A) || (pc == 0x0209)))
    {
        cpu.pc = 0x020F;
        cpu.wz = 0x020F;
        auto_floppy_key_sent_ = true;
    }
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
