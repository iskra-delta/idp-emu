#include "partner_crt.hpp"

#include <array>
#include <cstdio>
#include <string>
#include <vector>

namespace {

constexpr uint16_t INTERNAL_DATA_PORT  = 0xD8;
constexpr uint16_t INTERNAL_CTRL_PORT  = 0xD9;
constexpr uint16_t SECONDARY_A_DATA_PORT = 0xE0;
constexpr uint16_t SECONDARY_A_CTRL_PORT = 0xE1;
constexpr uint16_t SECONDARY_B_DATA_PORT = 0xE2;
constexpr uint16_t SECONDARY_B_CTRL_PORT = 0xE3;

class partner_crt_test : public partner_crt
{
public:
    using partner_crt::partner_crt;
    using partner_crt::io_write;
};

std::vector<std::string> split_lines(const std::string &text)
{
    std::vector<std::string> lines;
    std::string current;
    for (char ch : text) {
        if (ch == '\n') {
            lines.push_back(current);
            current.clear();
        } else {
            current.push_back(ch);
        }
    }
    return lines;
}

bool expect(bool cond, const char *label)
{
    if (!cond) {
        std::printf("FAIL %s\n", label);
        return false;
    }
    return true;
}

void send_tx_byte(partner_crt_test &emu, uint16_t port, uint8_t ch)
{
    emu.io_write(port, ch);
    for (int i = 0; i < 400; ++i)
        emu.tick();
}

void enable_rx(partner_crt_test &emu, uint16_t ctrl_port)
{
    emu.io_write(ctrl_port, 3);
    emu.io_write(ctrl_port, 1);
}

void enable_tx(partner_crt_test &emu, uint16_t ctrl_port)
{
    emu.io_write(ctrl_port, 5);
    emu.io_write(ctrl_port, 0x0A); // WR5: transmitter enable + RTS
}

uint8_t crc8(uint8_t value)
{
    uint8_t crc = value;
    for (unsigned int bit = 0; bit < 8; ++bit)
        crc = static_cast<uint8_t>(
            (crc & 0x80U) != 0 ? (crc << 1U) ^ 0x07U : crc << 1U);
    return crc;
}

} // namespace

int main()
{
    int fails = 0;

    {
        partner_crt_test emu(terminal_profile::vt52);
        fails += !expect(
            emu.get_sio_device_config(partner::sio_port_id::sio1_b).kind ==
                partner::sio_device_kind::internal_squid,
            "classic Partner defaults internal Squid to SIO1B");
        emu.reset();
        enable_tx(emu, INTERNAL_CTRL_PORT);
        enable_tx(emu, SECONDARY_B_CTRL_PORT);

        const uint8_t seq_internal[] = { 0x1B, 'Y', ' ', '"', 'A' };
        for (uint8_t ch : seq_internal)
            send_tx_byte(emu, INTERNAL_DATA_PORT, ch);

        const auto lines = split_lines(emu.dump_terminal_text());
        fails += !expect(!lines.empty(), "internal SIO writes reach terminal");
        fails += !expect(lines[0] == "  A", "internal SIO cursor/addressing works");

        const std::string before_secondary = emu.dump_terminal_text();
        const uint8_t seq_secondary[] = { 0x1B, 'Y', '!', ' ', 'B' };
        for (uint8_t ch : seq_secondary)
            send_tx_byte(emu, SECONDARY_B_DATA_PORT, ch);

        fails += !expect(emu.dump_terminal_text() == before_secondary,
                         "secondary SIO writes stay off the internal CRT");

        send_tx_byte(emu, INTERNAL_DATA_PORT, 0x1B);
        send_tx_byte(emu, INTERNAL_DATA_PORT, 'E');
        fails += !expect(emu.dump_terminal_text().find('A') == std::string::npos,
                         "internal SIO clear screen reaches terminal");
        fails += !expect(emu.dump_terminal_text().find('B') == std::string::npos,
                         "internal clear screen removes prior text");
    }

    {
        partner_crt_test emu(terminal_profile::vt52);
        emu.reset();
        enable_tx(emu, INTERNAL_CTRL_PORT);
        const char marker[] = "CP/M V3.0 Loader";
        for (char ch : marker)
            send_tx_byte(emu, INTERNAL_DATA_PORT, (uint8_t)ch);
        send_tx_byte(emu, INTERNAL_DATA_PORT, 'X');

        emu.debug_set_pc(0x2000);
        emu.tick();
        fails += !expect(
            emu.dump_raw_serial_text().find(marker) != std::string::npos &&
                emu.dump_raw_serial_text().find('X') != std::string::npos,
            "CP/M program execution at 2000h does not clear terminal output");
    }

    {
        partner_crt_test emu(terminal_profile::vt52);
        emu.reset();

        // GUI input is queued independently of the receiver-enable window.
        // Do not discard a key merely because firmware has not configured the
        // SIO yet; it must remain pending until the chip can receive it.
        emu.key_input('Q');
        for (int i = 0; i < 400; ++i)
            emu.tick();
        fails += !expect(emu.pending_key_count() == 1u,
                         "key waits while internal SIO RX is disabled");

        emu.reset();
        enable_rx(emu, INTERNAL_CTRL_PORT);
        enable_rx(emu, SECONDARY_A_CTRL_PORT);
        enable_rx(emu, SECONDARY_B_CTRL_PORT);
        emu.key_input('K');
        for (int i = 0; i < 400; ++i)
            emu.tick();

        const auto &sio = emu.get_sio();
        const auto &sio2 = emu.get_sio2();
        fails += !expect((sio.chn[Z80SIO_CHANNEL_A].wr[3] & 0x01u) != 0u,
                         "internal SIO RX is enabled");
        fails += !expect((sio2.chn[Z80SIO_CHANNEL_B].wr[3] & 0x01u) != 0u,
                         "secondary SIO B RX is enabled");
        fails += !expect(sio.chn[Z80SIO_CHANNEL_A].rx_ready,
                         "host key reaches internal SIO");
        fails += !expect(sio.chn[Z80SIO_CHANNEL_A].rx_data == 'K',
                         "internal SIO receives host key data");
        fails += !expect(!sio2.chn[Z80SIO_CHANNEL_A].rx_ready,
                         "host key stays off secondary SIO A");
        fails += !expect(!sio2.chn[Z80SIO_CHANNEL_B].rx_ready,
                         "host key stays off secondary SIO B");
    }

    {
        partner_crt_test emu(terminal_profile::vt52);
        emu.reset();
        enable_tx(emu, INTERNAL_CTRL_PORT + 2);

        const std::array<uint8_t, 3> hello = {
            0xE0U, crc8(0xE0U), 0xD3U
        };
        for (uint8_t byte : hello)
            send_tx_byte(emu, INTERNAL_DATA_PORT + 2, byte);

        const auto stale = emu.get_sio_port_status(
            partner::sio_port_id::sio1_b);
        fails += !expect(stale.connected,
                         "first PAKET session reaches internal Squid");
        fails += !expect(stale.pending_rx_bytes > 0,
                         "unread first-session response is queued");

        // PAKET starts each invocation with a WR0 channel-reset command.
        emu.io_write(INTERNAL_CTRL_PORT + 2, 0x18);
        const auto reset = emu.get_sio_port_status(
            partner::sio_port_id::sio1_b);
        fails += !expect(!reset.connected,
                         "guest SIO reset closes the previous Squid session");
        fails += !expect(reset.pending_rx_bytes == 0,
                         "guest SIO reset discards stale Squid response bytes");

        // A real OUT instruction supplies these four recovery clocks while
        // fetching the next opcode; this direct I/O test must do so itself.
        for (int i = 0; i < 4; ++i)
            emu.tick();
        enable_rx(emu, INTERNAL_CTRL_PORT + 2);
        enable_tx(emu, INTERNAL_CTRL_PORT + 2);
        for (uint8_t byte : hello)
            send_tx_byte(emu, INTERNAL_DATA_PORT + 2, byte);
        for (int i = 0; i < 10000; ++i)
            emu.tick();

        const auto fresh = emu.get_sio_port_status(
            partner::sio_port_id::sio1_b);
        fails += !expect(fresh.connected,
                         "second PAKET session reaches internal Squid");
        fails += !expect(fresh.rx_bytes > 0,
                         "second PAKET session receives a fresh handshake");
    }

    if (fails == 0) {
        std::puts("test_partner_crt_terminal: PASS");
        return 0;
    }

    std::printf("test_partner_crt_terminal: %d failure(s)\n", fails);
    return 1;
}
