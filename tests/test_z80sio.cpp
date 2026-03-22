#include <cstdint>
#include <cstdio>

#define CHIPS_IMPL
#include "z80sio.h"

namespace {

static uint64_t make_io_pins(int channel, bool control, bool read, uint8_t data, uint64_t extra_pins = 0)
{
    uint64_t pins = extra_pins | Z80SIO_CE | Z80SIO_IORQ | (read ? Z80SIO_RD : Z80SIO_WR);
    if (control) {
        pins |= Z80SIO_CS_A;
    }
    if (channel == Z80SIO_CHANNEL_B) {
        pins |= Z80SIO_CS_B;
    }
    if (!read) {
        Z80SIO_SET_DATA(pins, data);
    }
    return pins;
}

static void write_control(z80sio_t* sio, int channel, uint8_t data, uint64_t extra_pins = 0)
{
    uint64_t pins = make_io_pins(channel, true, false, data, extra_pins);
    (void)z80sio_tick(sio, pins);
}

static void write_data(z80sio_t* sio, int channel, uint8_t data, uint64_t extra_pins = 0)
{
    uint64_t pins = make_io_pins(channel, false, false, data, extra_pins);
    (void)z80sio_tick(sio, pins);
}

static uint8_t read_control(z80sio_t* sio, int channel, uint64_t extra_pins = 0)
{
    uint64_t pins = make_io_pins(channel, true, true, 0, extra_pins);
    pins = z80sio_tick(sio, pins);
    return Z80SIO_GET_DATA(pins);
}

static uint8_t read_data(z80sio_t* sio, int channel, uint64_t extra_pins = 0)
{
    uint64_t pins = make_io_pins(channel, false, true, 0, extra_pins);
    pins = z80sio_tick(sio, pins);
    return Z80SIO_GET_DATA(pins);
}

static void write_wr(z80sio_t* sio, int channel, uint8_t wr, uint8_t value, uint64_t extra_pins = 0)
{
    write_control(sio, channel, wr, extra_pins);
    write_control(sio, channel, value, extra_pins);
}

static int test_tx_data_path_and_control_data_select()
{
    int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

    z80sio_t sio{};
    z80sio_init(&sio);

    write_wr(&sio, Z80SIO_CHANNEL_A, 5, 0x68); // TX enable, 8-bit
    write_wr(&sio, Z80SIO_CHANNEL_B, 5, 0x68);

    write_data(&sio, Z80SIO_CHANNEL_A, 0x5A);
    write_data(&sio, Z80SIO_CHANNEL_B, 0xA5);

    CHECK(sio.chn[Z80SIO_CHANNEL_A].tx_ready == false);
    CHECK(sio.chn[Z80SIO_CHANNEL_B].tx_ready == false);
    CHECK(z80sio_tx_data(&sio, Z80SIO_CHANNEL_A) == 0x5A);
    CHECK(z80sio_tx_data(&sio, Z80SIO_CHANNEL_B) == 0xA5);
    CHECK(sio.chn[Z80SIO_CHANNEL_A].tx_ready == true);
    CHECK(sio.chn[Z80SIO_CHANNEL_B].tx_ready == true);

    sio.chn[Z80SIO_CHANNEL_A].parity_error = true;
    write_control(&sio, Z80SIO_CHANNEL_A, 0x01); // point to RR1
    const uint8_t rr1 = read_control(&sio, Z80SIO_CHANNEL_A);
    CHECK((rr1 & 0x10u) != 0u);

#undef CHECK
    return fails;
}

static int test_modem_input_output_and_rr0_layout()
{
    int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

    z80sio_t sio{};
    z80sio_init(&sio);

    write_wr(&sio, Z80SIO_CHANNEL_A, 5, 0xEA); // DTR, RTS, TX enable, 8-bit

    uint64_t pins = z80sio_tick(&sio, 0);
    CHECK((pins & Z80SIO_RTSA) != 0);
    CHECK((pins & Z80SIO_DTRA) != 0);

    const uint64_t modem_inputs = Z80SIO_DCDA | Z80SIO_CTSA;
    const uint8_t rr0 = read_control(&sio, Z80SIO_CHANNEL_A, modem_inputs);
    CHECK((rr0 & (1u << 3)) != 0u); // DCD
    CHECK((rr0 & (1u << 5)) != 0u); // CTS
    CHECK((rr0 & (1u << 6)) != 0u); // TX underrun/EOM set after reset
    CHECK((rr0 & (1u << 7)) == 0u); // Break/Abort

#undef CHECK
    return fails;
}

static int test_wr0_commands_error_reset_and_channel_reset()
{
    int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

    z80sio_t sio{};
    z80sio_init(&sio);

    write_wr(&sio, Z80SIO_CHANNEL_A, 4, 0x47); // x16, 1 stop, parity enabled even
    write_wr(&sio, Z80SIO_CHANNEL_A, 3, 0x41); // RX enable, 7-bit
    write_wr(&sio, Z80SIO_CHANNEL_A, 1, 0x10); // RX interrupt on all chars, parity special

    z80sio_rx_data(&sio, Z80SIO_CHANNEL_A, 0xB5); // wrong parity for 0x35 payload
    CHECK(sio.chn[Z80SIO_CHANNEL_A].parity_error == true);

    write_control(&sio, Z80SIO_CHANNEL_A, 0x30); // WR0: error reset command
    CHECK(sio.chn[Z80SIO_CHANNEL_A].parity_error == false);
    CHECK(sio.chn[Z80SIO_CHANNEL_A].rx_overrun == false);
    CHECK(sio.chn[Z80SIO_CHANNEL_A].framing_error == false);

    write_control(&sio, Z80SIO_CHANNEL_A, 0x18); // WR0: channel reset
    CHECK(sio.chn[Z80SIO_CHANNEL_A].wr[3] == 0);
    CHECK(sio.chn[Z80SIO_CHANNEL_A].wr[4] == 0);
    CHECK(sio.chn[Z80SIO_CHANNEL_A].wr[5] == 0);
    CHECK(sio.chn[Z80SIO_CHANNEL_A].rx_ready == false);
    CHECK(sio.chn[Z80SIO_CHANNEL_A].tx_ready == true);
    CHECK(sio.chn[Z80SIO_CHANNEL_A].tx_underrun == true);

#undef CHECK
    return fails;
}

static int test_receive_interrupt_modes()
{
    int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

    z80sio_t sio{};
    z80sio_init(&sio);

    write_wr(&sio, Z80SIO_CHANNEL_A, 3, 0xC1); // RX enable, 8-bit
    write_wr(&sio, Z80SIO_CHANNEL_A, 1, 0x08); // RX int on first char only

    z80sio_rx_data(&sio, Z80SIO_CHANNEL_A, 0x11);
    CHECK((sio.chn[Z80SIO_CHANNEL_A].int_state & Z80SIO_INT_NEEDED) != 0);

    (void)read_data(&sio, Z80SIO_CHANNEL_A);
    sio.chn[Z80SIO_CHANNEL_A].int_state = 0;

    z80sio_rx_data(&sio, Z80SIO_CHANNEL_A, 0x22);
    CHECK((sio.chn[Z80SIO_CHANNEL_A].int_state & Z80SIO_INT_NEEDED) == 0);

    write_control(&sio, Z80SIO_CHANNEL_A, 0x20); // enable int on next RX character
    z80sio_rx_data(&sio, Z80SIO_CHANNEL_A, 0x33);
    CHECK((sio.chn[Z80SIO_CHANNEL_A].int_state & Z80SIO_INT_NEEDED) != 0);

#undef CHECK
    return fails;
}

static int test_status_affects_vector_and_parity_modes()
{
    int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

    {
        z80sio_t sio{};
        z80sio_init(&sio);
        write_wr(&sio, Z80SIO_CHANNEL_B, 2, 0x40);
        write_wr(&sio, Z80SIO_CHANNEL_B, 1, 0x1C); // status affects vector + RX all chars mode 3
        write_wr(&sio, Z80SIO_CHANNEL_B, 3, 0xC1); // RX enable, 8-bit
        z80sio_rx_data(&sio, Z80SIO_CHANNEL_B, 0x66);

        uint64_t pins = Z80SIO_IEIO | Z80SIO_IORQ | Z80SIO_M1;
        pins = z80sio_tick(&sio, pins);
        CHECK(Z80SIO_GET_DATA(pins) == 0x44u); // Ch B RX
    }

    {
        z80sio_t sio{};
        z80sio_init(&sio);
        write_wr(&sio, Z80SIO_CHANNEL_B, 2, 0x40);
        write_wr(&sio, Z80SIO_CHANNEL_B, 4, 0x47); // parity enabled even
        write_wr(&sio, Z80SIO_CHANNEL_B, 3, 0x41); // RX enable, 7-bit

        // Mode 2: parity is special receive condition.
        write_wr(&sio, Z80SIO_CHANNEL_B, 1, 0x14);
        z80sio_rx_data(&sio, Z80SIO_CHANNEL_B, 0x15); // wrong parity bit for payload 0x15
        uint64_t pins = Z80SIO_IEIO | Z80SIO_IORQ | Z80SIO_M1;
        pins = z80sio_tick(&sio, pins);
        CHECK(Z80SIO_GET_DATA(pins) == 0x46u); // Ch B special receive

        // Mode 3: parity should not force special-receive vector.
        z80sio_reset(&sio);
        write_wr(&sio, Z80SIO_CHANNEL_B, 2, 0x40);
        write_wr(&sio, Z80SIO_CHANNEL_B, 4, 0x47);
        write_wr(&sio, Z80SIO_CHANNEL_B, 3, 0x41);
        write_wr(&sio, Z80SIO_CHANNEL_B, 1, 0x1C); // status affects + mode 3
        z80sio_rx_data(&sio, Z80SIO_CHANNEL_B, 0x15); // parity error present
        pins = Z80SIO_IEIO | Z80SIO_IORQ | Z80SIO_M1;
        pins = z80sio_tick(&sio, pins);
        CHECK(Z80SIO_GET_DATA(pins) == 0x44u); // Ch B RX (not special)
    }

#undef CHECK
    return fails;
}

static int test_tx_interrupt_on_buffer_empty()
{
    int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

    z80sio_t sio{};
    z80sio_init(&sio);
    write_wr(&sio, Z80SIO_CHANNEL_A, 1, 0x02); // TX interrupt enable
    write_wr(&sio, Z80SIO_CHANNEL_A, 5, 0x68); // TX enable

    write_data(&sio, Z80SIO_CHANNEL_A, 0xAA);
    CHECK((sio.chn[Z80SIO_CHANNEL_A].int_state & Z80SIO_INT_NEEDED) == 0);
    (void)z80sio_tx_data(&sio, Z80SIO_CHANNEL_A);
    CHECK((sio.chn[Z80SIO_CHANNEL_A].int_state & Z80SIO_INT_NEEDED) != 0);

#undef CHECK
    return fails;
}

} // namespace

int main()
{
    int fails = 0;
    fails += test_tx_data_path_and_control_data_select();
    fails += test_modem_input_output_and_rr0_layout();
    fails += test_wr0_commands_error_reset_and_channel_reset();
    fails += test_receive_interrupt_modes();
    fails += test_status_affects_vector_and_parity_modes();
    fails += test_tx_interrupt_on_buffer_empty();

    if (fails == 0) {
        std::printf("test_z80sio: all tests passed\n");
        return 0;
    }

    std::printf("test_z80sio: %d failure(s)\n", fails);
    return 1;
}
