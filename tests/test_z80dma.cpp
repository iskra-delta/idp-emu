#include <cstdint>
#include <cstdio>

#define CHIPS_IMPL
#include "z80dma.h"

namespace {
int test_separate_bus_cycles()
{
    int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)
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
#undef CHECK
    return fails;
}

int test_pin_level_interrupt_ack()
{
    int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)
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
#undef CHECK
    return fails;
}
} // namespace

int main()
{
    const int fails = test_separate_bus_cycles() + test_pin_level_interrupt_ack();
    if (!fails)
        std::puts("test_z80dma: all tests passed");
    return fails ? 1 : 0;
}
