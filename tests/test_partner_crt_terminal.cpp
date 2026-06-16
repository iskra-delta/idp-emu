#include "partner_crt.hpp"

#include <cstdio>
#include <string>
#include <vector>

namespace {

constexpr uint16_t SIO1A_DATA_PORT = 0xE0;
constexpr uint16_t SIO1A_CTRL_PORT = 0xE1;
constexpr uint16_t SIO1B_DATA_PORT = 0xE2;
constexpr uint16_t SIO1B_CTRL_PORT = 0xE3;

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

        const uint8_t seq_a[] = { 0x1B, 'Y', ' ', '"', 'A' };
        for (uint8_t ch : seq_a)
            send_tx_byte(emu, SIO1A_DATA_PORT, ch);

        const auto lines = split_lines(emu.dump_terminal_text());
        fails += !expect(!lines.empty(), "secondary SIO A writes reach terminal");
        fails += !expect(lines[0] == "  A", "secondary SIO A cursor/addressing works");

        const uint8_t seq_b[] = { 0x1B, 'Y', '!', ' ', 'B' };
        for (uint8_t ch : seq_b)
            send_tx_byte(emu, SIO1B_DATA_PORT, ch);

        const auto lines_b = split_lines(emu.dump_terminal_text());
        fails += !expect(lines_b.size() > 1, "secondary SIO B writes create second row");
        fails += !expect(lines_b[1] == "B", "secondary SIO B printable output works");

        send_tx_byte(emu, SIO1B_DATA_PORT, 0x1B);
        send_tx_byte(emu, SIO1B_DATA_PORT, 'E');
        fails += !expect(emu.dump_terminal_text().find('A') == std::string::npos,
                         "secondary SIO B clear screen reaches terminal");
        fails += !expect(emu.dump_terminal_text().find('B') == std::string::npos,
                         "secondary SIO B clear screen removes prior text");
    }

    {
        partner_crt_test emu(terminal_profile::vt52);
        emu.reset();
        enable_rx(emu, SIO1A_CTRL_PORT);
        enable_rx(emu, SIO1B_CTRL_PORT);
        emu.key_input('K');
        emu.tick();

        const auto &sio2 = emu.get_sio2();
        fails += !expect((sio2.chn[Z80SIO_CHANNEL_A].wr[3] & 0x01u) != 0u,
                         "secondary SIO A RX is enabled");
        fails += !expect((sio2.chn[Z80SIO_CHANNEL_B].wr[3] & 0x01u) != 0u,
                         "secondary SIO B RX is enabled");
        fails += !expect(sio2.chn[Z80SIO_CHANNEL_A].rx_ready,
                         "host key reaches secondary SIO A");
        fails += !expect(sio2.chn[Z80SIO_CHANNEL_A].rx_data == 'K',
                         "secondary SIO A receives host key data");
        fails += !expect(sio2.chn[Z80SIO_CHANNEL_B].rx_ready,
                         "host key reaches secondary SIO B");
        fails += !expect(sio2.chn[Z80SIO_CHANNEL_B].rx_data == 'K',
                         "secondary SIO B receives host key data");
    }

    if (fails == 0) {
        std::puts("test_partner_crt_terminal: PASS");
        return 0;
    }

    std::printf("test_partner_crt_terminal: %d failure(s)\n", fails);
    return 1;
}
