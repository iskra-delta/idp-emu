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

static void idle_ticks(z80sio_t* sio, int count, uint64_t pins = 0)
{
    while (count-- > 0)
        (void)z80sio_tick(sio, pins);
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
    CHECK(sio.chn[Z80SIO_CHANNEL_A].tx_shift_empty == false);
    write_control(&sio, Z80SIO_CHANNEL_A, 0x01); // point to RR1
    CHECK((read_control(&sio, Z80SIO_CHANNEL_A) & 0x01u) == 0u);
    z80sio_tx_complete(&sio, Z80SIO_CHANNEL_A);
    write_control(&sio, Z80SIO_CHANNEL_A, 0x01);
    CHECK((read_control(&sio, Z80SIO_CHANNEL_A) & 0x01u) != 0u);

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
    z80sio_rx_data(&sio, Z80SIO_CHANNEL_A, 0x22);
    CHECK((sio.chn[Z80SIO_CHANNEL_A].int_state & Z80SIO_INT_NEEDED) == 0);

    write_control(&sio, Z80SIO_CHANNEL_A, 0x20); // enable int on next RX character
    z80sio_rx_data(&sio, Z80SIO_CHANNEL_A, 0x33);
    CHECK((sio.chn[Z80SIO_CHANNEL_A].int_state & Z80SIO_INT_NEEDED) != 0);

#undef CHECK
    return fails;
}

static int test_channel_a_rr0_reports_either_channel_interrupt()
{
    int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

    z80sio_t sio{};
    z80sio_init(&sio);
    write_wr(&sio, Z80SIO_CHANNEL_B, 3, 0xC1); // RX enable, 8-bit
    write_wr(&sio, Z80SIO_CHANNEL_B, 1, 0x10); // RX interrupt on all chars
    z80sio_rx_data(&sio, Z80SIO_CHANNEL_B, 0x42);

    CHECK((read_control(&sio, Z80SIO_CHANNEL_A) & 0x02u) != 0u);
    CHECK((read_control(&sio, Z80SIO_CHANNEL_B) & 0x02u) == 0u);

#undef CHECK
    return fails;
}

static int test_three_character_receive_fifo_and_overrun()
{
    int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

    z80sio_t sio{};
    z80sio_init(&sio);
    write_wr(&sio, Z80SIO_CHANNEL_A, 3, 0xC1); // RX enable, 8-bit

    z80sio_rx_data(&sio, Z80SIO_CHANNEL_A, 0x11);
    z80sio_rx_data(&sio, Z80SIO_CHANNEL_A, 0x22);
    z80sio_rx_data(&sio, Z80SIO_CHANNEL_A, 0x33);
    CHECK(sio.chn[Z80SIO_CHANNEL_A].rx_fifo_count == 3u);
    CHECK(sio.chn[Z80SIO_CHANNEL_A].rx_overrun == false);

    z80sio_rx_data(&sio, Z80SIO_CHANNEL_A, 0x44);
    CHECK(sio.chn[Z80SIO_CHANNEL_A].rx_fifo_count == 3u);
    CHECK(sio.chn[Z80SIO_CHANNEL_A].rx_overrun == true);
    CHECK(read_data(&sio, Z80SIO_CHANNEL_A) == 0x11u);
    CHECK(read_data(&sio, Z80SIO_CHANNEL_A) == 0x22u);
    CHECK(read_data(&sio, Z80SIO_CHANNEL_A) == 0x44u);
    CHECK(sio.chn[Z80SIO_CHANNEL_A].rx_ready == false);
    CHECK(sio.chn[Z80SIO_CHANNEL_A].rx_fifo_count == 0u);

    write_control(&sio, Z80SIO_CHANNEL_A, 0x30); // error reset
    CHECK(sio.chn[Z80SIO_CHANNEL_A].rx_overrun == false);

#undef CHECK
    return fails;
}

static int test_receive_error_latches_until_error_reset()
{
    int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

    z80sio_t sio{};
    z80sio_init(&sio);
    write_wr(&sio, Z80SIO_CHANNEL_A, 4, 0x47); // x16, one stop, even parity
    write_wr(&sio, Z80SIO_CHANNEL_A, 3, 0x41); // RX enable, 7-bit
    z80sio_rx_data(&sio, Z80SIO_CHANNEL_A, 0xB5); // wrong parity
    CHECK(read_data(&sio, Z80SIO_CHANNEL_A) == 0xB5u);

    write_control(&sio, Z80SIO_CHANNEL_A, 0x01);
    CHECK((read_control(&sio, Z80SIO_CHANNEL_A) & 0x10u) != 0u);
    write_control(&sio, Z80SIO_CHANNEL_A, 0x30);
    write_control(&sio, Z80SIO_CHANNEL_A, 0x01);
    CHECK((read_control(&sio, Z80SIO_CHANNEL_A) & 0x10u) == 0u);

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

static int test_rts_deasserts_after_last_character()
{
    int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

    z80sio_t sio{};
    z80sio_init(&sio);
    write_wr(&sio, Z80SIO_CHANNEL_B, 5, 0xEA); // DTR, RTS, TX enable, 8-bit
    write_data(&sio, Z80SIO_CHANNEL_B, 0xA5);
    CHECK(z80sio_tx_data(&sio, Z80SIO_CHANNEL_B) == 0xA5);
    CHECK(sio.chn[Z80SIO_CHANNEL_B].rts == true);

    write_wr(&sio, Z80SIO_CHANNEL_B, 5, 0xE8); // request RTS low
    CHECK(sio.chn[Z80SIO_CHANNEL_B].rts == true);
    z80sio_tx_complete(&sio, Z80SIO_CHANNEL_B);
    CHECK(sio.chn[Z80SIO_CHANNEL_B].rts == false);
    CHECK(sio.chn[Z80SIO_CHANNEL_B].dtr == true);

#undef CHECK
    return fails;
}

static int test_chip_owned_line_timing()
{
    int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)
    z80sio_t sio{};
    z80sio_init(&sio);
    write_wr(&sio, Z80SIO_CHANNEL_A, 4, 0x04); // x1, one stop
    write_wr(&sio, Z80SIO_CHANNEL_A, 5, 0x68); // TX enable, 8-bit
    write_wr(&sio, Z80SIO_CHANNEL_A, 3, 0xC1); // RX enable, 8-bit
    write_data(&sio, Z80SIO_CHANNEL_A, 0xA5);

    const uint64_t tx_ticks = z80sio_character_ticks(
        &sio, Z80SIO_CHANNEL_A, true, 4000000, 153600);
    z80sio_line_tick(&sio, Z80SIO_CHANNEL_A, 100, 4000000, 153600);
    CHECK(z80sio_line_tx_busy(&sio, Z80SIO_CHANNEL_A));
    CHECK(sio.chn[Z80SIO_CHANNEL_A].tx_ready);
    uint8_t data = 0;
    CHECK(!z80sio_line_take_tx(&sio, Z80SIO_CHANNEL_A, &data));
    z80sio_line_tick(&sio, Z80SIO_CHANNEL_A, 100 + tx_ticks - 1,
                     4000000, 153600);
    CHECK(!z80sio_line_take_tx(&sio, Z80SIO_CHANNEL_A, &data));
    z80sio_line_tick(&sio, Z80SIO_CHANNEL_A, 100 + tx_ticks,
                     4000000, 153600);
    CHECK(z80sio_line_take_tx(&sio, Z80SIO_CHANNEL_A, &data));
    CHECK(data == 0xA5);
    CHECK(!z80sio_line_tx_busy(&sio, Z80SIO_CHANNEL_A));
    CHECK(sio.chn[Z80SIO_CHANNEL_A].tx_shift_empty);

    const uint64_t rx_start = 100 + tx_ticks;
    const uint64_t rx_ticks = z80sio_character_ticks(
        &sio, Z80SIO_CHANNEL_A, false, 4000000, 153600);
    CHECK(z80sio_line_receive(&sio, Z80SIO_CHANNEL_A, 0x5A, rx_start,
                              4000000, 153600));
    CHECK(z80sio_line_rx_busy(&sio, Z80SIO_CHANNEL_A));
    z80sio_line_tick(&sio, Z80SIO_CHANNEL_A, rx_start + rx_ticks - 1,
                     4000000, 153600);
    CHECK(!z80sio_rx_ready(&sio, Z80SIO_CHANNEL_A));
    z80sio_line_tick(&sio, Z80SIO_CHANNEL_A, rx_start + rx_ticks,
                     4000000, 153600);
    CHECK(z80sio_rx_ready(&sio, Z80SIO_CHANNEL_A));
    CHECK(read_data(&sio, Z80SIO_CHANNEL_A) == 0x5A);
#undef CHECK
    return fails;
}

static int test_partner_rom_initialization_and_reset_clocks()
{
    int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)
    z80sio_t sio{};
    z80sio_init(&sio);

    write_wr(&sio, Z80SIO_CHANNEL_B, 2, 0x10);
    write_control(&sio, Z80SIO_CHANNEL_B, 0x18); // Channel reset.
    CHECK(sio.chn[Z80SIO_CHANNEL_B].reset_cooldown == 4u);
    CHECK(sio.chn[Z80SIO_CHANNEL_B].int_vector == 0x10u);
    CHECK(sio.chn[Z80SIO_CHANNEL_A].int_vector == 0x10u);

    // Writes during the documented four-clock recovery interval are ignored.
    write_control(&sio, Z80SIO_CHANNEL_B, 0x04);
    CHECK(sio.chn[Z80SIO_CHANNEL_B].reg_index == 0u);
    idle_ticks(&sio, 3);
    CHECK(sio.chn[Z80SIO_CHANNEL_B].reset_cooldown == 0u);

    // Exact asynchronous sequence used by both Partner ROM variants.
    write_control(&sio, Z80SIO_CHANNEL_B, 0x04);
    write_control(&sio, Z80SIO_CHANNEL_B, 0x44);
    write_control(&sio, Z80SIO_CHANNEL_B, 0x03);
    write_control(&sio, Z80SIO_CHANNEL_B, 0xC1);
    write_control(&sio, Z80SIO_CHANNEL_B, 0x05);
    write_control(&sio, Z80SIO_CHANNEL_B, 0x68);
    CHECK(sio.chn[Z80SIO_CHANNEL_B].wr[4] == 0x44u);
    CHECK(sio.chn[Z80SIO_CHANNEL_B].wr[3] == 0xC1u);
    CHECK(sio.chn[Z80SIO_CHANNEL_B].wr[5] == 0x68u);
    CHECK(z80sio_rx_enabled(&sio, Z80SIO_CHANNEL_B));
    CHECK(z80sio_tx_ready(&sio, Z80SIO_CHANNEL_B));
#undef CHECK
    return fails;
}

static int test_all_asynchronous_frame_durations()
{
    int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)
    static constexpr uint64_t system_hz = 4000000;
    static constexpr uint64_t serial_clock_hz = 153600;
    static constexpr uint8_t divisors[4] = { 1, 16, 32, 64 };
    static constexpr uint8_t bits[4] = { 5, 7, 6, 8 };
    static constexpr uint8_t stop_half_bits[4] = { 0, 2, 3, 4 };

    z80sio_t sio{};
    z80sio_init(&sio);
    for (uint8_t clock = 0; clock < 4; ++clock)
    {
        for (uint8_t size = 0; size < 4; ++size)
        {
            for (uint8_t parity = 0; parity < 2; ++parity)
            {
                for (uint8_t stop = 1; stop < 4; ++stop)
                {
                    sio.chn[0].wr[4] = (uint8_t)((clock << 6) |
                        (stop << 2) | parity);
                    sio.chn[0].wr[3] = (uint8_t)(size << 6);
                    sio.chn[0].wr[5] = (uint8_t)(size << 5);
                    const uint64_t baud = serial_clock_hz / divisors[clock];
                    const uint64_t half_bits = 2u + bits[size] * 2u +
                        parity * 2u + stop_half_bits[stop];
                    const uint64_t expected =
                        (system_hz * half_bits + baud * 2u - 1u) /
                        (baud * 2u);
                    CHECK(z80sio_character_ticks(&sio, 0, false,
                              system_hz, serial_clock_hz) == expected);
                    CHECK(z80sio_character_ticks(&sio, 0, true,
                              system_hz, serial_clock_hz) == expected);
                }
            }
        }
    }
    // The external serial clock need not divide evenly by the clock-rate
    // divisor; duration is a rational clock count, not a truncated baud rate.
    sio.chn[0].wr[4] = 0x44; // x16, one stop, no parity.
    sio.chn[0].wr[3] = 0xC0;
    sio.chn[0].wr[5] = 0x60;
    const uint64_t rational_expected =
        (1000000u * 20u * 16u + 115201u * 2u - 1u) /
        (115201u * 2u);
    CHECK(z80sio_character_ticks(&sio, 0, true, 1000000, 115201) ==
          rational_expected);
#undef CHECK
    return fails;
}

static int test_fifo_error_correspondence()
{
    int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)
    z80sio_t sio{};
    z80sio_init(&sio);
    write_wr(&sio, 0, 3, 0xC1);
    write_wr(&sio, 0, 1, 0x18); // All RX chars, parity not special.
    z80sio_rx_data(&sio, 0, 0x11);
    z80sio_rx_data(&sio, 0, 0x22);
    z80sio_rx_data(&sio, 0, 0x33);
    z80sio_rx_data_ex(&sio, 0, 0x44, Z80SIO_RX_ERROR_FRAMING);

    CHECK(sio.chn[0].rx_fifo_count == 3u);
    CHECK(sio.chn[0].rx_fifo[2] == 0x44u);
    CHECK((sio.chn[0].rx_error_fifo[2] &
           (Z80SIO_RX_ERROR_OVERRUN | Z80SIO_RX_ERROR_FRAMING)) ==
          (Z80SIO_RX_ERROR_OVERRUN | Z80SIO_RX_ERROR_FRAMING));
    CHECK(!sio.chn[0].framing_error);
    CHECK(read_data(&sio, 0) == 0x11u);
    CHECK(!sio.chn[0].framing_error);
    CHECK(read_data(&sio, 0) == 0x22u);
    CHECK(sio.chn[0].framing_error);
    write_control(&sio, 0, 0x01);
    CHECK((read_control(&sio, 0) & 0x60u) == 0x60u);
    CHECK(read_data(&sio, 0) == 0x44u);
#undef CHECK
    return fails;
}

static int test_external_status_latch_and_reset()
{
    int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)
    z80sio_t sio{};
    z80sio_init(&sio);
    write_wr(&sio, 0, 1, 0x01); // External/Status interrupt enable.
    (void)z80sio_tick(&sio, Z80SIO_DCDA | Z80SIO_CTSA);
    CHECK(sio.chn[0].ext_status_latched);
    CHECK((sio.chn[0].int_source_state[Z80SIO_INT_EXTERNAL] &
           Z80SIO_INT_NEEDED) != 0);

    (void)z80sio_tick(&sio, 0); // A second transition cannot change latch.
    const uint8_t latched = read_control(&sio, 0);
    CHECK((latched & ((1u << 3) | (1u << 5))) ==
          ((1u << 3) | (1u << 5)));
    write_control(&sio, 0, 0x10); // Reset External/Status.
    CHECK(!sio.chn[0].ext_status_latched);
    CHECK((sio.chn[0].int_source_state[Z80SIO_INT_EXTERNAL] &
           (Z80SIO_INT_NEEDED | Z80SIO_INT_REQUESTED)) == 0);
    CHECK((read_control(&sio, 0) & ((1u << 3) | (1u << 5))) == 0u);
#undef CHECK
    return fails;
}

static int test_auto_enables_and_wait_ready()
{
    int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)
    z80sio_t sio{};
    z80sio_init(&sio);
    write_wr(&sio, 0, 3, 0xE1); // 8-bit RX, auto enables, RX enable.
    write_wr(&sio, 0, 5, 0x68); // 8-bit TX enable.
    write_data(&sio, 0, 0x5A);
    CHECK(!z80sio_tx_pending(&sio, 0));
    CHECK(!z80sio_rx_enabled(&sio, 0));
    CHECK(!z80sio_line_receive(&sio, 0, 0xA5, 0, 4000000, 153600));
    (void)z80sio_tick(&sio, Z80SIO_DCDA | Z80SIO_CTSA);
    CHECK(z80sio_tx_pending(&sio, 0));
    CHECK(z80sio_rx_enabled(&sio, 0));

    // READY on transmit follows transmit-buffer empty/full state.
    write_wr(&sio, 0, 1, 0xC0, Z80SIO_DCDA | Z80SIO_CTSA);
    uint64_t pins = z80sio_tick(&sio, Z80SIO_DCDA | Z80SIO_CTSA);
    CHECK((pins & Z80SIO_WRDYA) == 0); // Holding register still full.
    CHECK(z80sio_tx_data(&sio, 0) == 0x5Au);
    pins = z80sio_tick(&sio, Z80SIO_DCDA | Z80SIO_CTSA);
    CHECK((pins & Z80SIO_WRDYA) != 0);

    // WAIT on receive blocks an empty data read without consuming anything.
    write_wr(&sio, 0, 1, 0xA0, Z80SIO_DCDA | Z80SIO_CTSA);
    pins = make_io_pins(0, false, true, 0,
                        Z80SIO_DCDA | Z80SIO_CTSA);
    pins = z80sio_tick(&sio, pins);
    CHECK((pins & Z80SIO_WRDYA) != 0);
    z80sio_rx_data(&sio, 0, 0x33);
    pins = make_io_pins(0, false, true, 0,
                        Z80SIO_DCDA | Z80SIO_CTSA);
    pins = z80sio_tick(&sio, pins);
    CHECK((pins & Z80SIO_WRDYA) == 0);
    CHECK(Z80SIO_GET_DATA(pins) == 0x33u);

    // WAIT on transmit preserves a full holding register.
    write_wr(&sio, 0, 1, 0x80, Z80SIO_DCDA | Z80SIO_CTSA);
    write_data(&sio, 0, 0x66, Z80SIO_DCDA | Z80SIO_CTSA);
    pins = make_io_pins(0, false, false, 0x77,
                        Z80SIO_DCDA | Z80SIO_CTSA);
    pins = z80sio_tick(&sio, pins);
    CHECK((pins & Z80SIO_WRDYA) != 0);
    CHECK(sio.chn[0].tx_data == 0x66u);
#undef CHECK
    return fails;
}

static int test_interrupt_priority_vectors_and_rr2()
{
    int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)
    z80sio_t sio{};
    z80sio_init(&sio);
    write_wr(&sio, 1, 2, 0x40);
    write_wr(&sio, 1, 1, 0x17); // Ext, TX, status vector, RX all/parity special.
    write_wr(&sio, 0, 1, 0x13); // Ext, TX, RX all/parity special.
    for (int channel = 0; channel < 2; ++channel)
    {
        write_wr(&sio, channel, 3, 0xC1);
        write_wr(&sio, channel, 5, 0x68);
        write_data(&sio, channel, (uint8_t)(0x50 + channel));
        (void)z80sio_tx_data(&sio, channel);
        z80sio_rx_data(&sio, channel, (uint8_t)(0x60 + channel));
    }
    (void)z80sio_tick(&sio, Z80SIO_DCDA | Z80SIO_CTSA |
                            Z80SIO_DCDB | Z80SIO_CTSB);

    write_control(&sio, 1, 0x02, Z80SIO_DCDA | Z80SIO_CTSA |
                                 Z80SIO_DCDB | Z80SIO_CTSB);
    CHECK(read_control(&sio, 1, Z80SIO_DCDA | Z80SIO_CTSA |
                                Z80SIO_DCDB | Z80SIO_CTSB) == 0x4Cu);

    static constexpr uint8_t expected[6] = {
        0x4C, 0x48, 0x4A, 0x44, 0x40, 0x42
    };
    for (int i = 0; i < 6; ++i)
    {
        uint64_t pins = z80sio_tick(&sio,
            Z80SIO_IEIO | Z80SIO_IORQ | Z80SIO_M1 |
            Z80SIO_DCDA | Z80SIO_CTSA | Z80SIO_DCDB | Z80SIO_CTSB);
        CHECK(Z80SIO_GET_DATA(pins) == expected[i]);
        const int channel = i < 3 ? 0 : 1;
        if (i == 0 || i == 3)
            (void)read_data(&sio, channel, Z80SIO_DCDA | Z80SIO_CTSA |
                                           Z80SIO_DCDB | Z80SIO_CTSB);
        else if (i == 1 || i == 4)
            write_control(&sio, channel, 0x28, Z80SIO_DCDA | Z80SIO_CTSA |
                                                 Z80SIO_DCDB | Z80SIO_CTSB);
        else
            write_control(&sio, channel, 0x10, Z80SIO_DCDA | Z80SIO_CTSA |
                                                 Z80SIO_DCDB | Z80SIO_CTSB);
        (void)z80sio_tick(&sio, Z80SIO_RETI | Z80SIO_IEIO |
                                Z80SIO_DCDA | Z80SIO_CTSA |
                                Z80SIO_DCDB | Z80SIO_CTSB);
    }
    CHECK(sio.chn[0].int_state == 0u);
    CHECK(sio.chn[1].int_state == 0u);
    write_control(&sio, 1, 0x02, Z80SIO_DCDA | Z80SIO_CTSA |
                                 Z80SIO_DCDB | Z80SIO_CTSB);
    CHECK(read_control(&sio, 1, Z80SIO_DCDA | Z80SIO_CTSA |
                                Z80SIO_DCDB | Z80SIO_CTSB) == 0x46u);
#undef CHECK
    return fails;
}

static int test_interrupt_request_is_deferred_during_m1()
{
    int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

    z80sio_t sio{};
    z80sio_init(&sio);
    sio.chn[Z80SIO_CHANNEL_A].wr[1] = 0x18; // interrupt on every RX character
    sio.chn[Z80SIO_CHANNEL_A].wr[3] = 0xC1; // eight-bit receiver enabled

    uint64_t pins = z80sio_tick(&sio, Z80SIO_IEIO | Z80SIO_M1);
    z80sio_rx_data(&sio, Z80SIO_CHANNEL_A, 0x5A);
    CHECK(sio.chn[Z80SIO_CHANNEL_A].int_state == 0u);
    CHECK(sio.chn[Z80SIO_CHANNEL_A].int_deferred != 0u);

    pins = z80sio_tick(&sio, Z80SIO_IEIO | Z80SIO_M1);
    CHECK((pins & Z80SIO_INT) == 0u);
    pins = z80sio_tick(&sio, Z80SIO_IEIO);
    CHECK((pins & Z80SIO_INT) != 0u);
    CHECK((sio.chn[Z80SIO_CHANNEL_A].int_state &
           Z80SIO_INT_REQUESTED) != 0u);

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
    fails += test_channel_a_rr0_reports_either_channel_interrupt();
    fails += test_three_character_receive_fifo_and_overrun();
    fails += test_receive_error_latches_until_error_reset();
    fails += test_status_affects_vector_and_parity_modes();
    fails += test_tx_interrupt_on_buffer_empty();
    fails += test_rts_deasserts_after_last_character();
    fails += test_chip_owned_line_timing();
    fails += test_partner_rom_initialization_and_reset_clocks();
    fails += test_all_asynchronous_frame_durations();
    fails += test_fifo_error_correspondence();
    fails += test_external_status_latch_and_reset();
    fails += test_auto_enables_and_wait_ready();
    fails += test_interrupt_priority_vectors_and_rr2();
    fails += test_interrupt_request_is_deferred_during_m1();

    if (fails == 0) {
        std::printf("test_z80sio: all tests passed\n");
        return 0;
    }

    std::printf("test_z80sio: %d failure(s)\n", fails);
    return 1;
}
