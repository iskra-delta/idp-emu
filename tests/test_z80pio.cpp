#include <cstdint>
#include <cstdio>

#define CHIPS_IMPL
#include "z80pio.h"

namespace {

static uint64_t make_io_pins(int port, bool control, bool read, uint8_t data, uint64_t extra_pins = 0)
{
    uint64_t pins = extra_pins | Z80PIO_CE | Z80PIO_IORQ |
                    Z80PIO_ASTB | Z80PIO_BSTB;
    if (read) {
        pins |= Z80PIO_RD;
    }
    if (control) {
        pins |= Z80PIO_CDSEL;
    }
    if (port == Z80PIO_PORT_B) {
        pins |= Z80PIO_BASEL;
    }
    if (!read) {
        Z80PIO_SET_DATA(pins, data);
    }
    return pins;
}

static void write_control(z80pio_t* pio, int port, uint8_t data, uint64_t extra_pins = 0)
{
    uint64_t pins = make_io_pins(port, true, false, data, extra_pins);
    (void)z80pio_tick(pio, pins);
}

static void write_data(z80pio_t* pio, int port, uint8_t data, uint64_t extra_pins = 0)
{
    uint64_t pins = make_io_pins(port, false, false, data, extra_pins);
    (void)z80pio_tick(pio, pins);
}

static uint8_t read_data(z80pio_t* pio, int port, uint64_t extra_pins = 0)
{
    uint64_t pins = make_io_pins(port, false, true, 0, extra_pins);
    pins = z80pio_tick(pio, pins);
    return Z80PIO_GET_DATA(pins);
}

static int test_mode_constraints_and_m1_reset()
{
    int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

    z80pio_t pio{};
    z80pio_init(&pio);

    write_control(&pio, Z80PIO_PORT_A, 0x42); // vector write (even = vector)
    write_control(&pio, Z80PIO_PORT_B, 0x24);

    write_control(&pio, Z80PIO_PORT_A, 0x8F); // Mode 2 (bidirectional)
    write_control(&pio, Z80PIO_PORT_B, 0x8F); // Mode 2 request on B should be rejected
    CHECK(pio.port[Z80PIO_PORT_A].mode == Z80PIO_MODE_BIDIRECTIONAL);
    CHECK(pio.port[Z80PIO_PORT_B].mode != Z80PIO_MODE_BIDIRECTIONAL);

    write_control(&pio, Z80PIO_PORT_A, 0x83); // enable interrupts
    CHECK(pio.port[Z80PIO_PORT_A].int_enabled == true);

    (void)z80pio_tick(&pio, Z80PIO_M1 | Z80PIO_ASTB | Z80PIO_BSTB);
    CHECK(pio.reset_active == false); // reset occurs only after M1 returns inactive
    (void)z80pio_tick(&pio, Z80PIO_ASTB | Z80PIO_BSTB);
    CHECK(pio.reset_active == true);
    CHECK(pio.port[Z80PIO_PORT_A].mode == Z80PIO_MODE_INPUT);
    CHECK(pio.port[Z80PIO_PORT_B].mode == Z80PIO_MODE_INPUT);
    CHECK(pio.port[Z80PIO_PORT_A].int_enabled == false);
    CHECK(pio.port[Z80PIO_PORT_B].int_enabled == false);
    CHECK(pio.port[Z80PIO_PORT_A].int_vector == 0x42);
    CHECK(pio.port[Z80PIO_PORT_B].int_vector == 0x24);

#undef CHECK
    return fails;
}

static int test_output_handshake_active_low_strobe()
{
    int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

    z80pio_t pio{};
    z80pio_init(&pio);

    // Keep STB high (inactive) first so edge detection has a defined baseline.
    (void)z80pio_tick(&pio, Z80PIO_ASTB | Z80PIO_BSTB);

    write_control(&pio, Z80PIO_PORT_A, 0x0F, Z80PIO_ASTB); // Mode 0 output
    write_control(&pio, Z80PIO_PORT_A, 0x83, Z80PIO_ASTB); // Interrupt enable
    write_data(&pio, Z80PIO_PORT_A, 0x5A, Z80PIO_ASTB);
    CHECK(pio.port[Z80PIO_PORT_A].ready == true);

    uint64_t pins = z80pio_tick(&pio, Z80PIO_ASTB | Z80PIO_BSTB);
    CHECK((pins & Z80PIO_ARDY) != 0u);

    // Assert STB low: should not acknowledge yet.
    pins = z80pio_tick(&pio, Z80PIO_BSTB);
    CHECK(pio.port[Z80PIO_PORT_A].ready == true);
    CHECK((pio.port[Z80PIO_PORT_A].int_state & Z80PIO_INT_NEEDED) == 0);

    // Deassert STB high (rising edge): acknowledge transfer.
    pins = z80pio_tick(&pio, Z80PIO_ASTB | Z80PIO_BSTB);
    CHECK(pio.port[Z80PIO_PORT_A].ready == false);
    CHECK((pio.port[Z80PIO_PORT_A].int_state & Z80PIO_INT_NEEDED) != 0);
    CHECK((pins & Z80PIO_ARDY) == 0u);

#undef CHECK
    return fails;
}

static int test_input_handshake_and_cpu_read()
{
    int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

    z80pio_t pio{};
    z80pio_init(&pio);

    write_control(&pio, Z80PIO_PORT_B, 0x4F, Z80PIO_BSTB); // Mode 1 input
    write_control(&pio, Z80PIO_PORT_B, 0x83, Z80PIO_BSTB); // Interrupt enable

    // A read primes the input handshake: Ready high means another byte may
    // be strobed into the input register.
    (void)read_data(&pio, Z80PIO_PORT_B);
    CHECK(pio.port[Z80PIO_PORT_B].ready == true);

    uint64_t pins = Z80PIO_BSTB;
    Z80PIO_SET_PB(pins, 0x3C);
    (void)z80pio_tick(&pio, pins);

    // Assert STB low and place input data.
    pins = 0;
    Z80PIO_SET_PB(pins, 0x3C);
    (void)z80pio_tick(&pio, pins);
    CHECK(pio.port[Z80PIO_PORT_B].input == 0x3C);

    // Deassert STB high (rising edge): the register becomes full, Ready goes
    // low, and an interrupt is requested.
    pins = Z80PIO_BSTB;
    Z80PIO_SET_PB(pins, 0x3C);
    pins = z80pio_tick(&pio, pins);
    CHECK(pio.port[Z80PIO_PORT_B].ready == false);
    CHECK((pio.port[Z80PIO_PORT_B].int_state & Z80PIO_INT_NEEDED) != 0);
    CHECK((pins & Z80PIO_BRDY) == 0u);

    const uint8_t cpu_read = read_data(&pio, Z80PIO_PORT_B, Z80PIO_BSTB);
    CHECK(cpu_read == 0x3C);
    CHECK(pio.port[Z80PIO_PORT_B].ready == true);

#undef CHECK
    return fails;
}

static int test_interrupt_daisychain_request_hold_and_ack()
{
    int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

    z80pio_t pio{};
    z80pio_init(&pio);

    write_control(&pio, Z80PIO_PORT_A, 0xE2); // vector
    write_control(&pio, Z80PIO_PORT_A, 0x83); // EI
    pio.port[Z80PIO_PORT_A].int_state = Z80PIO_INT_NEEDED;

    uint64_t pins = z80pio_tick(&pio, Z80PIO_IEIO);
    CHECK((pins & Z80PIO_INT) != 0u);
    CHECK((pins & Z80PIO_IEIO) == 0u);
    CHECK((pio.port[Z80PIO_PORT_A].int_state & Z80PIO_INT_REQUESTED) != 0);

    // INT should stay asserted until the CPU acknowledges.
    pins = z80pio_tick(&pio, Z80PIO_IEIO);
    CHECK((pins & Z80PIO_INT) != 0u);

    pins = z80pio_tick(&pio, Z80PIO_IEIO | Z80PIO_IORQ | Z80PIO_M1);
    CHECK(Z80PIO_GET_DATA(pins) == 0xE2);
    CHECK((pio.port[Z80PIO_PORT_A].int_state & Z80PIO_INT_SERVICED) != 0);
    CHECK((pins & Z80PIO_INT) == 0u);

    pins = z80pio_tick(&pio, Z80PIO_IEIO | Z80PIO_RETI);
    CHECK((pio.port[Z80PIO_PORT_A].int_state & Z80PIO_INT_SERVICED) == 0);
    CHECK((pins & Z80PIO_IEIO) != 0u);

#undef CHECK
    return fails;
}

static int test_bitcontrol_combined_data_and_edge_trigger()
{
    int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

    z80pio_t pio{};
    z80pio_init(&pio);

    write_control(&pio, Z80PIO_PORT_A, 0xCF); // Mode 3 bit-control
    write_control(&pio, Z80PIO_PORT_A, 0x01); // bit0 input, others output
    write_data(&pio, Z80PIO_PORT_A, 0xA0);

    // EI=1, OR/LOW, mask follows.
    write_control(&pio, Z80PIO_PORT_A, 0x97);
    write_control(&pio, Z80PIO_PORT_A, 0xFE); // unmask bit0 only

    pio.port[Z80PIO_PORT_A].int_state = 0;
    pio.port[Z80PIO_PORT_A].bctrl_match = false;

    uint64_t pins = 0;
    Z80PIO_SET_PA(pins, 0x01); // input bit high -> no match for OR/LOW
    pins = z80pio_tick(&pio, pins);
    CHECK((pio.port[Z80PIO_PORT_A].int_state & Z80PIO_INT_NEEDED) == 0);
    CHECK(Z80PIO_GET_PA(pins) == 0xA1);
    CHECK(read_data(&pio, Z80PIO_PORT_A) == 0xA1);

    pins = 0;
    Z80PIO_SET_PA(pins, 0x00); // input bit low -> match
    (void)z80pio_tick(&pio, pins);
    CHECK((pio.port[Z80PIO_PORT_A].int_state & Z80PIO_INT_NEEDED) != 0);

    // Edge-triggered only: holding the same matching level shouldn't retrigger.
    pio.port[Z80PIO_PORT_A].int_state = 0;
    (void)z80pio_tick(&pio, pins);
    CHECK((pio.port[Z80PIO_PORT_A].int_state & Z80PIO_INT_NEEDED) == 0);

    pins = 0;
    Z80PIO_SET_PA(pins, 0x01); // release match
    (void)z80pio_tick(&pio, pins);
    pins = 0;
    Z80PIO_SET_PA(pins, 0x00); // assert match again
    (void)z80pio_tick(&pio, pins);
    CHECK((pio.port[Z80PIO_PORT_A].int_state & Z80PIO_INT_NEEDED) != 0);

#undef CHECK
    return fails;
}

static int test_interrupt_pending_while_disabled_and_m1_deferral()
{
    int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

    z80pio_t pio{};
    z80pio_init(&pio);
    write_control(&pio, Z80PIO_PORT_A, 0xE0); // vector, interrupts remain disabled
    write_control(&pio, Z80PIO_PORT_A, 0x0F); // mode 0 output
    write_data(&pio, Z80PIO_PORT_A, 0x55);

    (void)z80pio_tick(&pio, Z80PIO_BSTB); // ASTB asserted low
    (void)z80pio_tick(&pio, Z80PIO_ASTB | Z80PIO_BSTB); // rising edge
    CHECK((pio.port[Z80PIO_PORT_A].int_state & Z80PIO_INT_NEEDED) != 0);
    uint64_t pins = z80pio_tick(&pio, Z80PIO_IEIO | Z80PIO_ASTB | Z80PIO_BSTB);
    CHECK((pins & Z80PIO_INT) == 0u);
    CHECK((pins & Z80PIO_IEIO) != 0u);

    write_control(&pio, Z80PIO_PORT_A, 0x83); // enable existing pending cause
    pins = z80pio_tick(&pio, Z80PIO_IEIO | Z80PIO_ASTB | Z80PIO_BSTB);
    CHECK((pins & Z80PIO_INT) != 0u);

    z80pio_reset(&pio);
    write_control(&pio, Z80PIO_PORT_A, 0xCF); // mode 3
    write_control(&pio, Z80PIO_PORT_A, 0xFF); // all inputs
    write_control(&pio, Z80PIO_PORT_A, 0xB7); // OR/high, enabled, mask follows
    write_control(&pio, Z80PIO_PORT_A, 0xFE); // monitor PA0
    pins = Z80PIO_M1 | Z80PIO_RD | Z80PIO_ASTB | Z80PIO_BSTB;
    Z80PIO_SET_PA(pins, 0x01);
    (void)z80pio_tick(&pio, pins);
    CHECK((pio.port[Z80PIO_PORT_A].int_state & Z80PIO_INT_NEEDED) == 0);
    CHECK(pio.port[Z80PIO_PORT_A].interrupt_deferred == true);
    pins = Z80PIO_IEIO | Z80PIO_ASTB | Z80PIO_BSTB;
    Z80PIO_SET_PA(pins, 0x01);
    pins = z80pio_tick(&pio, pins);
    CHECK((pins & Z80PIO_INT) != 0u);

#undef CHECK
    return fails;
}

static int test_bitcontrol_all_logic_equations_and_all_masked()
{
    int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

    struct equation_case { uint8_t control; uint8_t false_value; uint8_t true_value; };
    const equation_case cases[] = {
        { 0x97, 0x03, 0x02 }, // OR/low
        { 0xB7, 0x00, 0x01 }, // OR/high
        { 0xD7, 0x01, 0x00 }, // AND/low
        { 0xF7, 0x01, 0x03 }, // AND/high
    };
    for (const auto &tc : cases) {
        z80pio_t pio{};
        z80pio_init(&pio);
        write_control(&pio, Z80PIO_PORT_A, 0xCF);
        write_control(&pio, Z80PIO_PORT_A, 0xFF);
        write_control(&pio, Z80PIO_PORT_A, tc.control);
        write_control(&pio, Z80PIO_PORT_A, 0xFC); // monitor PA1..PA0
        pio.port[Z80PIO_PORT_A].int_state = 0;
        uint64_t pins = Z80PIO_ASTB | Z80PIO_BSTB;
        Z80PIO_SET_PA(pins, tc.false_value);
        (void)z80pio_tick(&pio, pins);
        CHECK((pio.port[Z80PIO_PORT_A].int_state & Z80PIO_INT_NEEDED) == 0);
        Z80PIO_SET_PA(pins, tc.true_value);
        (void)z80pio_tick(&pio, pins);
        CHECK((pio.port[Z80PIO_PORT_A].int_state & Z80PIO_INT_NEEDED) != 0);
    }

    z80pio_t masked{};
    z80pio_init(&masked);
    write_control(&masked, Z80PIO_PORT_A, 0xCF);
    write_control(&masked, Z80PIO_PORT_A, 0xFF);
    write_control(&masked, Z80PIO_PORT_A, 0xF7); // AND/high
    write_control(&masked, Z80PIO_PORT_A, 0xFF); // no monitored lines
    uint64_t pins = Z80PIO_ASTB | Z80PIO_BSTB;
    Z80PIO_SET_PA(pins, 0xFF);
    (void)z80pio_tick(&masked, pins);
    CHECK(masked.port[Z80PIO_PORT_A].int_state == 0);

#undef CHECK
    return fails;
}

static int test_bidirectional_mode_uses_port_a_bus_and_b_handshake()
{
    int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

    z80pio_t pio{};
    z80pio_init(&pio);
    write_control(&pio, Z80PIO_PORT_A, 0xE2); // output interrupt vector
    write_control(&pio, Z80PIO_PORT_B, 0xE4); // input interrupt vector
    write_control(&pio, Z80PIO_PORT_B, 0xCF); // mode 3 required by mode 2
    write_control(&pio, Z80PIO_PORT_B, 0xFF);
    write_control(&pio, Z80PIO_PORT_A, 0x8F); // mode 2
    write_control(&pio, Z80PIO_PORT_A, 0x83);
    write_control(&pio, Z80PIO_PORT_B, 0x83);

    write_data(&pio, Z80PIO_PORT_A, 0xA5);
    uint64_t pins = Z80PIO_BSTB; // ASTB low: PIO drives Port A
    Z80PIO_SET_PA(pins, 0x5A);
    pins = z80pio_tick(&pio, pins);
    CHECK(Z80PIO_GET_PA(pins) == 0xA5);
    pins |= Z80PIO_ASTB;
    (void)z80pio_tick(&pio, pins);
    CHECK(!pio.port[Z80PIO_PORT_A].ready);
    CHECK((pio.port[Z80PIO_PORT_A].int_state & Z80PIO_INT_NEEDED) != 0);

    // Reading Port A primes the input half (BRDY), then BSTB latches external
    // data from the same Port A bus and requests Port B's vector.
    (void)read_data(&pio, Z80PIO_PORT_A);
    CHECK(pio.port[Z80PIO_PORT_B].ready);
    pins = Z80PIO_ASTB; // BSTB low
    Z80PIO_SET_PA(pins, 0x3C);
    (void)z80pio_tick(&pio, pins);
    pins |= Z80PIO_BSTB;
    (void)z80pio_tick(&pio, pins);
    CHECK(!pio.port[Z80PIO_PORT_B].ready);
    CHECK((pio.port[Z80PIO_PORT_B].int_state & Z80PIO_INT_NEEDED) != 0);
    CHECK(read_data(&pio, Z80PIO_PORT_A) == 0x3C);
    CHECK(pio.port[Z80PIO_PORT_B].ready);

#undef CHECK
    return fails;
}

} // namespace

int main()
{
    int fails = 0;
    fails += test_mode_constraints_and_m1_reset();
    fails += test_output_handshake_active_low_strobe();
    fails += test_input_handshake_and_cpu_read();
    fails += test_interrupt_daisychain_request_hold_and_ack();
    fails += test_bitcontrol_combined_data_and_edge_trigger();
    fails += test_interrupt_pending_while_disabled_and_m1_deferral();
    fails += test_bitcontrol_all_logic_equations_and_all_masked();
    fails += test_bidirectional_mode_uses_port_a_bus_and_b_handshake();

    if (fails == 0) {
        std::printf("test_z80pio: all tests passed\n");
        return 0;
    }

    std::printf("test_z80pio: %d failure(s)\n", fails);
    return 1;
}
