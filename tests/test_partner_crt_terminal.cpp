#include "partner_crt.hpp"

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
    emu.tick();
}

void enable_rx(partner_crt_test &emu, uint16_t ctrl_port)
{
    emu.io_write(ctrl_port, 3);
    emu.io_write(ctrl_port, 1);
}

} // namespace

int main()
{
    int fails = 0;

    {
        partner_crt_test emu(terminal_profile::vt52);
        emu.reset();

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
        enable_rx(emu, INTERNAL_CTRL_PORT);
        enable_rx(emu, SECONDARY_A_CTRL_PORT);
        enable_rx(emu, SECONDARY_B_CTRL_PORT);
        emu.key_input('K');
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

    if (fails == 0) {
        std::puts("test_partner_crt_terminal: PASS");
        return 0;
    }

    std::printf("test_partner_crt_terminal: %d failure(s)\n", fails);
    return 1;
}
