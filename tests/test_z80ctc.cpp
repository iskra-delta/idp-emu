#include <cstdint>
#include <cstdio>

#define CHIPS_IMPL
#include "z80ctc.h"

namespace {

int failures = 0;

#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

uint64_t tick(z80ctc_t &ctc, uint64_t pins = 0)
{
    return z80ctc_tick(&ctc, pins | Z80CTC_IEIO);
}

uint64_t write_channel(z80ctc_t &ctc, int channel, uint8_t data)
{
    uint64_t pins = Z80CTC_CE | Z80CTC_IORQ | Z80CTC_IEIO;
    if (channel & 1)
        pins |= Z80CTC_CS0;
    if (channel & 2)
        pins |= Z80CTC_CS1;
    Z80CTC_SET_DATA(pins, data);
    return z80ctc_tick(&ctc, pins);
}

uint64_t rising_edge(z80ctc_t &ctc, int channel)
{
    tick(ctc, 0);
    return tick(ctc, Z80CTC_CLKTRG0 << channel);
}

void test_timer_period_and_pulse_width()
{
    z80ctc_t ctc{};
    z80ctc_init(&ctc);
    write_channel(ctc, 0, 0x07); // timer, /16, auto, constant follows, reset
    write_channel(ctc, 0, 2);

    for (int clock = 1; clock < 32; ++clock)
        CHECK((tick(ctc) & Z80CTC_ZCTO0) == 0);
    CHECK((tick(ctc) & Z80CTC_ZCTO0) != 0);
    CHECK((tick(ctc) & Z80CTC_ZCTO0) == 0);
    CHECK(ctc.chn[0].down_counter == 2);
}

void test_external_trigger_delay_and_edge_selection()
{
    z80ctc_t ctc{};
    z80ctc_init(&ctc);
    write_channel(ctc, 0, 0x1F); // timer, rising external start, constant follows, reset
    write_channel(ctc, 0, 1);

    for (int i = 0; i < 20; ++i)
        CHECK((tick(ctc) & Z80CTC_ZCTO0) == 0);
    CHECK(ctc.chn[0].waiting_for_trigger);

    CHECK((tick(ctc, Z80CTC_CLKTRG0) & Z80CTC_ZCTO0) == 0);
    CHECK(!ctc.chn[0].waiting_for_trigger);
    CHECK((tick(ctc, Z80CTC_CLKTRG0) & Z80CTC_ZCTO0) == 0);
    for (int clock = 1; clock < 16; ++clock)
        CHECK((tick(ctc) & Z80CTC_ZCTO0) == 0);
    CHECK((tick(ctc) & Z80CTC_ZCTO0) != 0);

    z80ctc_reset(&ctc);
    write_channel(ctc, 0, 0x47); // counter, falling edge, constant follows, reset
    write_channel(ctc, 0, 1);
    CHECK((tick(ctc, Z80CTC_CLKTRG0) & Z80CTC_ZCTO0) == 0);
    CHECK((tick(ctc, 0) & Z80CTC_ZCTO0) != 0);
}

void test_counter_zero_means_256_and_reprogram_at_zero()
{
    z80ctc_t ctc{};
    z80ctc_init(&ctc);
    write_channel(ctc, 0, 0x57); // counter, rising edge, constant follows, reset
    write_channel(ctc, 0, 0);
    for (int edge = 1; edge < 256; ++edge)
        CHECK((rising_edge(ctc, 0) & Z80CTC_ZCTO0) == 0);
    CHECK((rising_edge(ctc, 0) & Z80CTC_ZCTO0) != 0);
    CHECK(ctc.chn[0].down_counter == 0);

    z80ctc_reset(&ctc);
    write_channel(ctc, 0, 0x57);
    write_channel(ctc, 0, 3);
    rising_edge(ctc, 0);
    CHECK(ctc.chn[0].down_counter == 2);
    write_channel(ctc, 0, 0x55); // update control and constant without reset
    write_channel(ctc, 0, 2);
    CHECK(ctc.chn[0].constant == 3);
    CHECK(ctc.chn[0].constant_pending);
    rising_edge(ctc, 0);
    CHECK((rising_edge(ctc, 0) & Z80CTC_ZCTO0) != 0);
    CHECK(ctc.chn[0].constant == 2);
    CHECK(ctc.chn[0].down_counter == 2);

    // Selecting the opposite slope changes only the selector. It is not an
    // input transition and therefore must not decrement the counter.
    tick(ctc, 0);
    write_channel(ctc, 0, 0x41);
    CHECK(ctc.chn[0].down_counter == 2);
}

void test_partner_channel_cascade()
{
    z80ctc_t ctc{};
    z80ctc_init(&ctc);
    write_channel(ctc, 0, 0x57);
    write_channel(ctc, 0, 2);
    write_channel(ctc, 1, 0x57);
    write_channel(ctc, 1, 2);

    uint64_t ch0 = rising_edge(ctc, 0);
    CHECK((ch0 & Z80CTC_ZCTO0) == 0);
    ch0 = rising_edge(ctc, 0);
    CHECK((ch0 & Z80CTC_ZCTO0) != 0);
    CHECK((tick(ctc, Z80CTC_CLKTRG1) & Z80CTC_ZCTO1) == 0);
    tick(ctc, 0);

    rising_edge(ctc, 0);
    ch0 = rising_edge(ctc, 0);
    CHECK((ch0 & Z80CTC_ZCTO0) != 0);
    CHECK((tick(ctc, Z80CTC_CLKTRG1) & Z80CTC_ZCTO1) != 0);
}

void test_vectors_priority_and_reti()
{
    z80ctc_t ctc{};
    z80ctc_init(&ctc);
    write_channel(ctc, 0, 0xE0);
    for (int channel = 0; channel < 2; ++channel) {
        write_channel(ctc, channel, 0xD7); // interrupting rising-edge counter
        write_channel(ctc, channel, 1);
        rising_edge(ctc, channel);
    }

    uint64_t pins = tick(ctc);
    CHECK((pins & Z80CTC_INT) != 0);
    pins = tick(ctc, Z80CTC_M1 | Z80CTC_IORQ);
    CHECK(Z80CTC_GET_DATA(pins) == 0xE0);
    CHECK((ctc.chn[0].int_state & Z80CTC_INT_SERVICED) != 0);
    pins = tick(ctc);
    CHECK((pins & Z80CTC_INT) == 0);
    tick(ctc, Z80CTC_RETI);
    pins = tick(ctc);
    CHECK((pins & Z80CTC_INT) != 0);
    pins = tick(ctc, Z80CTC_M1 | Z80CTC_IORQ);
    CHECK(Z80CTC_GET_DATA(pins) == 0xE2);
}

} // namespace

int main()
{
    test_timer_period_and_pulse_width();
    test_external_trigger_delay_and_edge_selection();
    test_counter_zero_means_256_and_reprogram_at_zero();
    test_partner_channel_cascade();
    test_vectors_priority_and_reti();
    if (failures == 0)
        std::puts("test_z80ctc: all tests passed");
    return failures == 0 ? 0 : 1;
}
