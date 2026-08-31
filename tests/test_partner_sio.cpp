#include "partner.hpp"

#include <array>
#include <cstdio>
#include <initializer_list>

namespace {

class partner_sio_test : public partner
{
public:
    partner_sio_test() : partner(
        std::string(IDP_SOURCE_ROOT) + "/tests/dump/partner-sio.nvram") {}

    using partner::io_read;
    using partner::io_write;

    std::array<uint8_t, 16> observed_ports{};
    std::array<uint8_t, 16> observed_data{};
    size_t observed_writes = 0;

    void load_program(std::initializer_list<uint8_t> bytes)
    {
        rom_enabled = false;
        ram_bank = 1;
        size_t offset = 0;
        for (uint8_t byte : bytes)
            ram[offset++] = byte;
    }

    void set_channel_b_parity_error()
    {
        sio.chn[Z80SIO_CHANNEL_B].parity_error = true;
    }

    void io_write(uint16_t port, uint8_t data) override
    {
        const uint8_t low_port = (uint8_t)port;
        if (low_port >= 0xD8u && low_port <= 0xDBu) {
            if (observed_writes < observed_ports.size()) {
                observed_ports[observed_writes] = low_port;
                observed_data[observed_writes] = data;
            }
            ++observed_writes;
        }
        partner::io_write(port, data);
    }
};

void configure_9600_8n1(partner_sio_test &emu)
{
    emu.io_write(0xDB, 0x18); // channel reset
    // Zilog requires four system clocks after a channel-reset command.
    for (int i = 0; i < 4; ++i)
        emu.tick();
    emu.io_write(0xDB, 0x04);
    emu.io_write(0xDB, 0x44); // x16 clock, one stop, no parity
    emu.io_write(0xDB, 0x05);
    emu.io_write(0xDB, 0x68); // TX enable, 8-bit
    emu.io_write(0xDB, 0x03);
    emu.io_write(0xDB, 0xC1); // RX enable, 8-bit
}

template <std::size_t N>
std::size_t receive_mouse_bytes(partner_sio_test &emu,
                                std::array<uint8_t, N> &bytes,
                                std::size_t received,
                                std::size_t wanted,
                                int tick_limit)
{
    for (int tick = 0; tick < tick_limit && received < wanted; ++tick) {
        emu.tick();
        if (emu.get_sio().chn[Z80SIO_CHANNEL_B].rx_ready)
            bytes[received++] = emu.io_read(0xDA);
    }
    return received;
}

} // namespace

int main()
{
    int failures = 0;
#define CHECK(condition) do { \
    if (!(condition)) { \
        std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        ++failures; \
    } \
} while (0)

    // The Z80 core keeps back-to-back OUT bus states adjacent. Every logical
    // OUT must still be dispatched once, with no lost register/value pair and
    // no second level-sensitive decode in the peripheral pass.
    partner_sio_test bus_emu;
    bus_emu.load_program({
        0x3E, 0x18, 0xD3, 0xDB,
        0x3E, 0x04, 0xD3, 0xDB,
        0x3E, 0x44, 0xD3, 0xDB,
        0x3E, 0x05, 0xD3, 0xDB,
        0x3E, 0x68, 0xD3, 0xDB,
        0x3E, 0xA5, 0xD3, 0xDA,
        0x76
    });
    for (int i = 0; i < 500; ++i)
        bus_emu.tick();
    const std::array<uint8_t, 6> expected_data = {
        0x18, 0x04, 0x44, 0x05, 0x68, 0xA5
    };
    CHECK(bus_emu.observed_writes == expected_data.size());
    for (size_t i = 0;
         i < expected_data.size() && i < bus_emu.observed_writes;
         ++i) {
        CHECK(bus_emu.observed_data[i] == expected_data[i]);
    }

    // A selected RR1 read must not be followed by a second implicit RR0 read.
    partner_sio_test read_emu;
    read_emu.io_write(0xDB, 0x01);
    read_emu.set_channel_b_parity_error();
    read_emu.load_program({ 0xDB, 0xDB, 0x76 }); // IN A,(DBh); HALT
    for (int i = 0; i < 100; ++i)
        read_emu.tick();
    const uint8_t read_value = (uint8_t)(read_emu.get_cpu().af >> 8);
    CHECK((read_value & 0x10u) != 0u);
    CHECK((read_value & 0x40u) == 0u); // RR0 TX-underrun bit must not leak in

    // Disconnected, quiescent serial lines have no clocked state of their own.
    // This guards a scheduler fast path which may omit empty line-adapter
    // calls, while the timed transmit/receive checks below ensure active lines
    // still complete on the exact original guest clock.
    partner_sio_test idle_emu;
    struct line_state {
        bool tx_active;
        uint8_t tx_data;
        uint64_t tx_complete_tick;
        bool tx_event_pending;
        uint8_t tx_event_data;
        bool rx_active;
        uint8_t rx_data;
        uint64_t rx_complete_tick;
        bool rx_event_pending;
        uint8_t rx_event_data;
        bool rx_event_accepted;
    };
    const auto capture_line = [](const z80sio_t &chip, int channel) {
        const z80sio_channel_t &line = chip.chn[channel];
        return line_state{
            line.line_tx_active, line.line_tx_data,
            line.line_tx_complete_tick, line.line_tx_event_pending,
            line.line_tx_event_data, line.line_rx_active, line.line_rx_data,
            line.line_rx_complete_tick, line.line_rx_event_pending,
            line.line_rx_event_data, line.line_rx_event_accepted
        };
    };
    const std::array<line_state, 4> idle_before = {
        capture_line(idle_emu.get_sio(), Z80SIO_CHANNEL_A),
        capture_line(idle_emu.get_sio(), Z80SIO_CHANNEL_B),
        capture_line(idle_emu.get_sio2(), Z80SIO_CHANNEL_A),
        capture_line(idle_emu.get_sio2(), Z80SIO_CHANNEL_B)
    };
    for (int i = 0; i < 8192; ++i)
        idle_emu.tick();
    const std::array<line_state, 4> idle_after = {
        capture_line(idle_emu.get_sio(), Z80SIO_CHANNEL_A),
        capture_line(idle_emu.get_sio(), Z80SIO_CHANNEL_B),
        capture_line(idle_emu.get_sio2(), Z80SIO_CHANNEL_A),
        capture_line(idle_emu.get_sio2(), Z80SIO_CHANNEL_B)
    };
    for (size_t i = 0; i < idle_before.size(); ++i) {
        CHECK(idle_after[i].tx_active == idle_before[i].tx_active);
        CHECK(idle_after[i].tx_data == idle_before[i].tx_data);
        CHECK(idle_after[i].tx_complete_tick == idle_before[i].tx_complete_tick);
        CHECK(idle_after[i].tx_event_pending == idle_before[i].tx_event_pending);
        CHECK(idle_after[i].tx_event_data == idle_before[i].tx_event_data);
        CHECK(idle_after[i].rx_active == idle_before[i].rx_active);
        CHECK(idle_after[i].rx_data == idle_before[i].rx_data);
        CHECK(idle_after[i].rx_complete_tick == idle_before[i].rx_complete_tick);
        CHECK(idle_after[i].rx_event_pending == idle_before[i].rx_event_pending);
        CHECK(idle_after[i].rx_event_data == idle_before[i].rx_event_data);
        CHECK(idle_after[i].rx_event_accepted == idle_before[i].rx_event_accepted);
    }

    // Changing a virtual cable must update the SIO's physical CTS/DCD inputs
    // on the very next motherboard clock, including disconnecting it again.
    partner_sio_test modem_emu;
    partner::sio_device_config modem_mouse;
    modem_mouse.kind = partner::sio_device_kind::mouse_microsoft;
    CHECK(modem_emu.set_sio_device_config(
        partner::sio_port_id::sio1_b, modem_mouse));
    modem_emu.tick();
    CHECK(modem_emu.get_sio().chn[Z80SIO_CHANNEL_B].cts);
    CHECK(modem_emu.get_sio().chn[Z80SIO_CHANNEL_B].dcd);
    partner::sio_device_config disconnected;
    disconnected.kind = partner::sio_device_kind::none;
    CHECK(modem_emu.set_sio_device_config(
        partner::sio_port_id::sio1_b, disconnected));
    modem_emu.tick();
    CHECK(!modem_emu.get_sio().chn[Z80SIO_CHANNEL_B].cts);
    CHECK(!modem_emu.get_sio().chn[Z80SIO_CHANNEL_B].dcd);

    // External serial characters take real wire time. At 9600 8N1 the fixed
    // 153600-Hz SIO clock and x16 mode produce 4167 Partner CPU ticks/byte.
    partner_sio_test timed_emu;
    partner::sio_device_config mouse;
    mouse.kind = partner::sio_device_kind::mouse_microsoft;
    CHECK(timed_emu.set_sio_device_config(partner::sio_port_id::sio1_b, mouse));
    configure_9600_8n1(timed_emu);
    timed_emu.io_write(0xDA, 0x5A);
    timed_emu.tick(); // holding register -> shift register
    // The real SIO exposes an empty holding register while the first byte is
    // still in the shift register, so software may queue one successor.
    timed_emu.io_write(0xDA, 0xA5);
    CHECK(!timed_emu.get_sio().chn[Z80SIO_CHANNEL_B].tx_ready);
    CHECK(timed_emu.get_sio_port_status(partner::sio_port_id::sio1_b).tx_bytes == 0);
    for (int i = 0; i < 4166; ++i)
        timed_emu.tick();
    CHECK(timed_emu.get_sio_port_status(partner::sio_port_id::sio1_b).tx_bytes == 0);
    timed_emu.tick();
    CHECK(timed_emu.get_sio_port_status(partner::sio_port_id::sio1_b).tx_bytes == 1);
    CHECK(timed_emu.get_sio().chn[Z80SIO_CHANNEL_B].tx_ready);
    CHECK(!timed_emu.get_sio().chn[Z80SIO_CHANNEL_B].tx_shift_empty);
    for (int i = 0; i < 4166; ++i)
        timed_emu.tick();
    CHECK(timed_emu.get_sio_port_status(partner::sio_port_id::sio1_b).tx_bytes == 1);
    timed_emu.tick();
    CHECK(timed_emu.get_sio_port_status(partner::sio_port_id::sio1_b).tx_bytes == 2);
    CHECK(timed_emu.get_sio().chn[Z80SIO_CHANNEL_B].tx_shift_empty);

    timed_emu.inject_serial_mouse_motion(1, 2, false, false, false);
    timed_emu.tick(); // begin receiving the first framed byte
    CHECK(!timed_emu.get_sio().chn[Z80SIO_CHANNEL_B].rx_ready);
    for (int i = 0; i < 4166; ++i)
        timed_emu.tick();
    CHECK(!timed_emu.get_sio().chn[Z80SIO_CHANNEL_B].rx_ready);
    timed_emu.tick();
    CHECK(timed_emu.get_sio().chn[Z80SIO_CHANNEL_B].rx_ready);
    CHECK(timed_emu.get_sio_port_status(partner::sio_port_id::sio1_b).rx_bytes == 1);

    // A foreground-polled guest may spend much longer than one character
    // time painting its cursor.  Keep the next virtual-mouse byte on the
    // cable until the current hardware byte is consumed, so a complete
    // protocol packet cannot overrun the SIO FIFO while no IRQ owns it.
    partner_sio_test polled_emu;
    CHECK(polled_emu.set_sio_device_config(
        partner::sio_port_id::sio1_b, mouse));
    configure_9600_8n1(polled_emu);
    polled_emu.inject_serial_mouse_motion(3, -2, false, false, false);
    for (int i = 0; i < 5000; ++i)
        polled_emu.tick();
    CHECK(polled_emu.get_sio().chn[Z80SIO_CHANNEL_B].rx_fifo_count == 1u);
    CHECK(polled_emu.get_sio_port_status(
        partner::sio_port_id::sio1_b).pending_rx_bytes == 2u);
    for (int i = 0; i < 50000; ++i)
        polled_emu.tick();
    CHECK(polled_emu.get_sio().chn[Z80SIO_CHANNEL_B].rx_fifo_count == 1u);
    CHECK((polled_emu.io_read(0xDA) & 0x40u) != 0u);
    for (int i = 0; i < 5000; ++i)
        polled_emu.tick();
    CHECK(polled_emu.get_sio().chn[Z80SIO_CHANNEL_B].rx_fifo_count == 1u);
    CHECK(polled_emu.get_sio_port_status(
        partner::sio_port_id::sio1_b).pending_rx_bytes == 1u);

    // Host pointer devices commonly produce several motion events per video
    // frame, much faster than a serial mouse can shift packets. Preserve the
    // complete accumulated motion without growing an unbounded byte backlog
    // (or dropping individual bytes and destroying packet alignment).
    partner_sio_test burst_emu;
    CHECK(burst_emu.set_sio_device_config(
        partner::sio_port_id::sio1_b, mouse));
    for (int i = 0; i < 1000; ++i)
        burst_emu.inject_serial_mouse_motion(1, -1, false, false, false);
    CHECK(burst_emu.get_sio_port_status(
        partner::sio_port_id::sio1_b).pending_rx_bytes <= 3u);

    partner_sio_test systems_burst_emu;
    partner::sio_device_config systems_mouse;
    systems_mouse.kind = partner::sio_device_kind::mouse_mousesystems;
    CHECK(systems_burst_emu.set_sio_device_config(
        partner::sio_port_id::sio1_b, systems_mouse));
    for (int i = 0; i < 1000; ++i)
        systems_burst_emu.inject_serial_mouse_motion(1, -1, false, false, false);
    CHECK(systems_burst_emu.get_sio_port_status(
        partner::sio_port_id::sio1_b).pending_rx_bytes <= 5u);

    // A guest mouse driver starts a new coordinate session by resetting its
    // SIO channel. Boot-time host movement must not survive that reset or the
    // newly centered guest pointer will remain offset by half a screen.
    partner_sio_test session_emu;
    CHECK(session_emu.set_sio_device_config(
        partner::sio_port_id::sio1_b, mouse));
    const auto generation_before_reset = session_emu.get_sio_port_status(
        partner::sio_port_id::sio1_b).session_generation;
    session_emu.inject_serial_mouse_motion(
        600, 300, false, false, false);
    CHECK(session_emu.get_sio_port_status(
        partner::sio_port_id::sio1_b).pending_rx_bytes == 3u);
    session_emu.io_write(0xDB, 0x18);
    const auto reset_status = session_emu.get_sio_port_status(
        partner::sio_port_id::sio1_b);
    CHECK(reset_status.pending_rx_bytes == 0u);
    CHECK(reset_status.session_generation == generation_before_reset + 1u);

    // Leaving the emulated display cancels accumulated host movement instead
    // of letting a fast host pointer keep moving the guest for seconds. The
    // packet already on the wire is allowed to finish intact.
    partner_sio_test leave_emu;
    CHECK(leave_emu.set_sio_device_config(
        partner::sio_port_id::sio1_b, mouse));
    configure_9600_8n1(leave_emu);
    for (int i = 0; i < 10000; ++i)
        leave_emu.inject_serial_mouse_motion(1, 1, false, false, false);
    leave_emu.deactivate_serial_mouse_input();
    std::array<uint8_t, 16> leave_bytes{};
    const std::size_t leave_received = receive_mouse_bytes(
        leave_emu, leave_bytes, 0, leave_bytes.size(), 100000);
    CHECK(leave_received == 3u);
    CHECK(leave_emu.get_sio_port_status(
        partner::sio_port_id::sio1_b).pending_rx_bytes == 0u);

    // Button-only transitions are complete Microsoft packets and survive the
    // same paced serial path used by movement.
    partner_sio_test click_emu;
    CHECK(click_emu.set_sio_device_config(
        partner::sio_port_id::sio1_b, mouse));
    configure_9600_8n1(click_emu);
    std::array<uint8_t, 9> click_bytes{};
    click_emu.inject_serial_mouse_motion(0, 0, false, false, false);
    CHECK(receive_mouse_bytes(click_emu, click_bytes, 0, 3, 50000) == 3u);
    click_emu.inject_serial_mouse_motion(0, 0, true, false, false);
    CHECK(receive_mouse_bytes(click_emu, click_bytes, 3, 6, 50000) == 6u);
    click_emu.inject_serial_mouse_motion(0, 0, false, false, false);
    CHECK(receive_mouse_bytes(click_emu, click_bytes, 6, 9, 50000) == 9u);
    CHECK((click_bytes[0] & 0x20u) == 0u);
    CHECK((click_bytes[3] & 0x20u) != 0u);
    CHECK((click_bytes[6] & 0x20u) == 0u);

    // A quick drag can queue down, movement, and up before the serial wire
    // drains. Movement accumulated while down must retain the down state and
    // precede the release packet.
    partner_sio_test drag_emu;
    CHECK(drag_emu.set_sio_device_config(
        partner::sio_port_id::sio1_b, mouse));
    configure_9600_8n1(drag_emu);
    drag_emu.inject_serial_mouse_motion(0, 0, false, false, false);
    drag_emu.inject_serial_mouse_motion(0, 0, true, false, false);
    drag_emu.inject_serial_mouse_motion(-100, -40, true, false, false);
    drag_emu.inject_serial_mouse_motion(0, 0, false, false, false);
    std::array<uint8_t, 12> drag_bytes{};
    CHECK(receive_mouse_bytes(
        drag_emu, drag_bytes, 0, drag_bytes.size(), 100000) ==
        drag_bytes.size());
    CHECK((drag_bytes[0] & 0x20u) == 0u);
    CHECK((drag_bytes[3] & 0x20u) != 0u);
    CHECK((drag_bytes[6] & 0x20u) != 0u);
    CHECK((drag_bytes[9] & 0x20u) == 0u);
    CHECK(drag_bytes[7] != 0u || drag_bytes[8] != 0u);
    CHECK(drag_bytes[10] == 0u && drag_bytes[11] == 0u);

    // The SDK's Partner LOGI/Genius state machine consumes the documented
    // fixed 60-byte response to 'c' before it starts issuing P polls.
    partner_sio_test logitech_emu;
    partner::sio_device_config logitech_mouse;
    logitech_mouse.kind = partner::sio_device_kind::mouse_logitech;
    CHECK(logitech_emu.set_sio_device_config(
        partner::sio_port_id::sio1_b, logitech_mouse));
    configure_9600_8n1(logitech_emu);
    logitech_emu.io_write(0xDA, 'c');
    for (int i = 0; i < 5000; ++i)
        logitech_emu.tick();
    CHECK(logitech_emu.get_sio().chn[Z80SIO_CHANNEL_B].line_rx_active);
    CHECK(logitech_emu.get_sio_port_status(
        partner::sio_port_id::sio1_b).pending_rx_bytes == 59u);

#undef CHECK
    if (failures == 0) {
        std::puts("test_partner_sio: all tests passed");
        return 0;
    }
    std::printf("test_partner_sio: %d failure(s)\n", failures);
    return 1;
}
