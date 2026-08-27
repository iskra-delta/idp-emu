#include <cstdint>
#include <cstdio>
#include <initializer_list>

#define CHIPS_IMPL
#include "z80dma.h"

namespace {
int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

void program(z80dma_t &dma, std::initializer_list<uint8_t> bytes)
{
    for (uint8_t byte : bytes)
        z80dma_write(&dma, byte);
}

int test_separate_bus_cycles()
{
    const int before = failures;
    z80dma_t dma{};
    z80dma_init(&dma);
    dma.enabled = true;
    dma.status = Z80DMA_STATUS_BUSY;
    dma.direction_ab = true;
    dma.mode = Z80DMA_MODE_CONTINUOUS;
    dma.port_a = {0x1234, 0x1234, 1, 1, 0, true, true, false};
    dma.port_b = {0x0056, 0x0056, 1, 1, 0, false, false, false};

    uint64_t pins = z80dma_tick(&dma, Z80DMA_RDY);
    CHECK(dma.state == Z80DMA_STATE_WAIT_BUS);
    CHECK((pins & Z80DMA_BUSREQ) != 0);

    pins = z80dma_tick(&dma, pins | Z80DMA_RDY | Z80DMA_BUSACK);
    CHECK(dma.state == Z80DMA_STATE_READ);

    pins = z80dma_tick(&dma, pins | Z80DMA_RDY | Z80DMA_BUSACK);
    CHECK(dma.state == Z80DMA_STATE_READ_LATCH);
    CHECK((pins & (Z80DMA_MREQ | Z80DMA_RD)) == (Z80DMA_MREQ | Z80DMA_RD));
    CHECK((pins & (Z80DMA_IORQ | Z80DMA_WR)) == 0);
    CHECK(Z80DMA_GET_ADDR(pins) == 0x1234);

    Z80DMA_SET_DATA(pins, 0xA5);
    pins = z80dma_tick(&dma, pins | Z80DMA_BUSACK);
    CHECK(dma.state == Z80DMA_STATE_WRITE);
    CHECK(dma.data_latch == 0xA5);
    CHECK((pins & (Z80DMA_MREQ | Z80DMA_IORQ | Z80DMA_RD | Z80DMA_WR)) == 0);
    CHECK(dma.port_a.address == 0x1235);

    pins = z80dma_tick(&dma, pins | Z80DMA_BUSACK);
    CHECK(dma.state == Z80DMA_STATE_IDLE);
    CHECK((pins & (Z80DMA_IORQ | Z80DMA_WR)) == (Z80DMA_IORQ | Z80DMA_WR));
    CHECK((pins & (Z80DMA_MREQ | Z80DMA_RD)) == 0);
    CHECK(Z80DMA_GET_ADDR(pins) == 0x0056);
    CHECK(Z80DMA_GET_DATA(pins) == 0xA5);
    CHECK(!dma.enabled);
    CHECK(dma.port_a.block_length == 0);
    return failures - before;
}

int test_pin_level_interrupt_ack()
{
    const int before = failures;
    z80dma_t dma{};
    z80dma_init(&dma);
    dma.int_vector = 0x9A;
    dma.int_state = Z80DMA_INT_NEEDED;

    uint64_t pins = z80dma_daisychain(&dma, Z80DMA_IEIO);
    CHECK((pins & Z80DMA_INT) != 0);
    CHECK((pins & Z80DMA_IEIO) == 0);
    CHECK((dma.int_state & Z80DMA_INT_REQUESTED) != 0);

    pins = z80dma_daisychain(&dma, Z80DMA_IEIO);
    CHECK((pins & Z80DMA_INT) != 0);
    CHECK((dma.int_state & Z80DMA_INT_REQUESTED) != 0);

    pins = z80dma_daisychain(
        &dma, Z80DMA_IEIO | Z80DMA_M1 | Z80DMA_IORQ);
    CHECK(Z80DMA_GET_DATA(pins) == 0x9A);
    CHECK((dma.int_state & Z80DMA_INT_SERVICED) != 0);
    CHECK((pins & Z80DMA_INT) == 0);
    pins = z80dma_daisychain(&dma, Z80DMA_IEIO | Z80DMA_RETI);
    CHECK((dma.int_state & Z80DMA_INT_SERVICED) == 0);
    CHECK((pins & Z80DMA_IEIO) != 0);
    return failures - before;
}

int test_zilog_sample_program()
{
    const int before = failures;
    z80dma_t dma{};
    z80dma_init(&dma);

    // Zilog UM0081 Table 16: 1001h bytes from memory 1050h to fixed I/O 05h.
    program(dma, {0x79, 0x50, 0x10, 0x00, 0x10,
                  0x14, 0x28, 0xC5, 0x05, 0x8A,
                  0xCF, 0x05, 0xCF, 0x87});
    CHECK(dma.port_a.start_address == 0x1050);
    CHECK(dma.port_a.address == 0x1050);
    CHECK(dma.port_a.is_memory);
    CHECK(dma.port_a.increment);
    CHECK(!dma.port_a.decrement);
    CHECK(dma.port_b.start_address == 0x0005);
    CHECK(!dma.port_b.is_memory);
    CHECK(!dma.port_b.increment && !dma.port_b.decrement);
    CHECK(dma.programmed_length == 0x1000);
    CHECK(dma.bytes_remaining == 0x1001u);
    CHECK(dma.direction_ab);
    CHECK(dma.mode == Z80DMA_MODE_BURST);
    CHECK(dma.ready_active_high);
    CHECK(!dma.wait_enabled);
    CHECK(dma.enabled);
    CHECK(dma.compat_state == 0);
    return failures - before;
}

int test_partner_rom_programs()
{
    const int before = failures;
    z80dma_t dma{};
    z80dma_init(&dma);

    // Partner floppy table: 256 bytes from FDC data port F1h to RAM 5100h.
    program(dma, {0x79, 0x00, 0x51, 0xFF, 0x00,
                  0x14, 0x28, 0x85, 0xF1, 0x8A,
                  0xCF, 0x01, 0xCF, 0x87});
    CHECK(dma.port_a.address == 0x5100);
    CHECK(dma.port_b.address == 0x00F1);
    CHECK(dma.bytes_remaining == 256u);
    CHECK(!dma.direction_ab);
    CHECK(dma.mode == Z80DMA_MODE_BYTE);
    CHECK(dma.port_a.is_memory && !dma.port_b.is_memory);
    CHECK(dma.port_a.increment);
    CHECK(!dma.port_b.increment && !dma.port_b.decrement);
    CHECK(dma.enabled);

    z80dma_reset(&dma);
    // Partner G SASI table includes WR4 interrupt control plus a vector.
    program(dma, {0x79, 0x00, 0xE0, 0xFF, 0x1E,
                  0x14, 0x28, 0x95, 0x11, 0x00, 0x8A,
                  0xCF, 0x01, 0xCF, 0x87});
    CHECK(dma.port_b.address == 0x0011);
    CHECK(dma.int_vector == 0x00);
    CHECK(dma.bytes_remaining == 0x1F00u);
    CHECK(dma.enabled);

    z80dma_reset(&dma);
    // Partner P's longer floppy table programs the WR4 pulse-control byte.
    program(dma, {0x79, 0x00, 0xE0, 0xFF, 0x00,
                  0x14, 0x28, 0x95, 0xF1, 0x0C, 0xFF, 0x8A,
                  0xCF, 0x01, 0xCF, 0x87});
    CHECK(dma.port_b.address == 0x00F1);
    CHECK(dma.pulse_control == 0xFF);
    CHECK(dma.bytes_remaining == 256u);
    CHECK(dma.enabled);
    return failures - before;
}

void setup_two_byte_transfer(z80dma_t &dma, z80dma_transfer_mode_t mode)
{
    z80dma_init(&dma);
    dma.enabled = true;
    dma.status = Z80DMA_STATUS_BUSY;
    dma.direction_ab = true;
    dma.mode = mode;
    dma.ready_active_high = true;
    dma.bytes_remaining = 2;
    dma.port_a = {0x2000, 0x2000, 2, 2, 0, true, true, false};
    dma.port_b = {0x0010, 0x0010, 2, 2, 0, false, false, false};
}

uint64_t transfer_through_read(z80dma_t &dma)
{
    uint64_t pins = z80dma_tick(&dma, Z80DMA_RDY);
    pins = z80dma_tick(&dma, pins | Z80DMA_RDY | Z80DMA_BUSACK);
    pins = z80dma_tick(&dma, pins | Z80DMA_RDY | Z80DMA_BUSACK);
    CHECK((pins & (Z80DMA_MREQ | Z80DMA_RD)) == (Z80DMA_MREQ | Z80DMA_RD));
    Z80DMA_SET_DATA(pins, 0x5A);
    // RDY falls after the read starts. The byte must still finish orderly.
    pins = z80dma_tick(&dma, (pins | Z80DMA_BUSACK) & ~Z80DMA_RDY);
    CHECK(dma.state == Z80DMA_STATE_WRITE);
    return z80dma_tick(&dma, (pins | Z80DMA_BUSACK) & ~Z80DMA_RDY);
}

int test_transfer_modes_and_ready()
{
    const int before = failures;
    z80dma_t dma{};

    setup_two_byte_transfer(dma, Z80DMA_MODE_BYTE);
    uint64_t pins = transfer_through_read(dma);
    CHECK((pins & (Z80DMA_IORQ | Z80DMA_WR)) == (Z80DMA_IORQ | Z80DMA_WR));
    CHECK(dma.data_latch == 0x5A);
    CHECK(dma.bytes_remaining == 1);
    CHECK(dma.state == Z80DMA_STATE_IDLE);
    CHECK((pins & Z80DMA_BUSREQ) == 0);
    for (int clock = 0; clock < 4; ++clock) {
        pins = z80dma_tick(&dma, Z80DMA_RDY);
        CHECK((pins & Z80DMA_BUSREQ) == 0);
    }
    pins = z80dma_tick(&dma, Z80DMA_RDY);
    CHECK((pins & Z80DMA_BUSREQ) != 0);

    setup_two_byte_transfer(dma, Z80DMA_MODE_BURST);
    pins = transfer_through_read(dma);
    CHECK(dma.state == Z80DMA_STATE_IDLE);
    CHECK((pins & Z80DMA_BUSREQ) == 0);
    pins = z80dma_tick(&dma, Z80DMA_RDY);
    CHECK((pins & Z80DMA_BUSREQ) != 0);

    setup_two_byte_transfer(dma, Z80DMA_MODE_CONTINUOUS);
    pins = transfer_through_read(dma);
    CHECK(dma.state == Z80DMA_STATE_WAIT_READY);
    CHECK((pins & Z80DMA_BUSREQ) != 0);
    pins = z80dma_tick(&dma, pins | Z80DMA_BUSACK);
    CHECK(dma.state == Z80DMA_STATE_WAIT_READY);
    CHECK((pins & Z80DMA_BUSREQ) != 0);
    pins = z80dma_tick(&dma, pins | Z80DMA_BUSACK | Z80DMA_RDY);
    CHECK(dma.state == Z80DMA_STATE_READ_LATCH);
    CHECK((pins & Z80DMA_BUSREQ) != 0);
    return failures - before;
}

int test_ready_polarity_force_ready_and_eob_interrupt()
{
    const int before = failures;
    z80dma_t dma{};
    z80dma_init(&dma);
    CHECK(z80dma_read(&dma) == 0x38u); // no IRQ, match, or EOB; RDY inactive
    program(dma, {0x82}); // WR5, RDY active low
    dma.enabled = true;
    dma.bytes_remaining = 1;
    uint64_t pins = z80dma_tick(&dma, 0);
    CHECK((pins & Z80DMA_BUSREQ) != 0);
    CHECK(z80dma_read(&dma) == 0x3Bu);

    z80dma_reset(&dma);
    program(dma, {0x8A, 0xB3, 0x87}); // active-high RDY, force it internally
    dma.bytes_remaining = 1;
    pins = z80dma_tick(&dma, 0);
    CHECK((pins & Z80DMA_BUSREQ) != 0);

    setup_two_byte_transfer(dma, Z80DMA_MODE_BYTE);
    dma.bytes_remaining = 1;
    dma.port_a.block_length = 1;
    dma.port_b.block_length = 1;
    dma.interrupt_enable = true;
    dma.interrupt_on_eob = true;
    pins = transfer_through_read(dma);
    CHECK(!dma.enabled);
    CHECK((dma.status & Z80DMA_STATUS_EOB) != 0);
    CHECK((dma.int_state & Z80DMA_INT_NEEDED) != 0);
    const uint8_t status = z80dma_read(&dma);
    CHECK((status & 0x20u) == 0); // official RR0 EOB indication is active low
    CHECK((status & 0x08u) == 0); // interrupt pending is also active low

    // WR6 read masks expose the live byte and address counters in RR1..RR6.
    dma.byte_counter = 0x1234;
    dma.port_a.address = 0x5678;
    dma.port_b.address = 0x9ABC;
    program(dma, {0xBB, 0x7E, 0xA7});
    CHECK(z80dma_read(&dma) == 0x34);
    CHECK(z80dma_read(&dma) == 0x12);
    CHECK(z80dma_read(&dma) == 0x78);
    CHECK(z80dma_read(&dma) == 0x56);
    CHECK(z80dma_read(&dma) == 0xBC);
    CHECK(z80dma_read(&dma) == 0x9A);
    return failures - before;
}
} // namespace

int main()
{
    const int fails = test_separate_bus_cycles() + test_pin_level_interrupt_ack() +
        test_zilog_sample_program() + test_partner_rom_programs() +
        test_transfer_modes_and_ready() +
        test_ready_polarity_force_ready_and_eob_interrupt();
    if (!fails)
        std::puts("test_z80dma: all tests passed");
    return fails ? 1 : 0;
}
