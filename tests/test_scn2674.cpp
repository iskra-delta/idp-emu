#include <cstdio>

#define CHIPS_IMPL
#include "scn2674.h"

namespace {

static int test_interrupt_command_groups()
{
    int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

    scn2674_t avdc{};
    scn2674_init(&avdc);

    avdc.status_latch = 0x1Fu;
    avdc.irq_status = 0x1Fu;
    scn2674_write(&avdc, 0x35, 0x50); // reset VBLANK status/irq bit
    CHECK(avdc.status_latch == 0x0Fu);
    CHECK(avdc.irq_status == 0x0Fu);

    avdc.irq_mask = 0x1Fu;
    scn2674_write(&avdc, 0x35, 0x70); // disable VBLANK interrupt bit
    CHECK(avdc.irq_mask == 0x0Fu);

    avdc.irq_mask = 0x00u;
    scn2674_write(&avdc, 0x35, 0x90); // enable VBLANK interrupt bit
    CHECK(avdc.irq_mask == 0x10u);

#undef CHECK
    return fails;
}

static int test_ir_pointer_load_command()
{
    int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

    scn2674_t avdc{};
    scn2674_init(&avdc);

    scn2674_write(&avdc, 0x35, 0x1C); // IR12
    CHECK(avdc.ir_ptr == 12);
    scn2674_write(&avdc, 0x34, 0x34);
    CHECK(avdc.ir[12] == 0x34);
    CHECK(avdc.ir_ptr == 13);

    scn2674_write(&avdc, 0x35, 0x1E); // IR14
    CHECK(avdc.ir_ptr == 14);
    scn2674_write(&avdc, 0x34, 0x56);
    CHECK(avdc.ir[14] == 0x56);
    CHECK(avdc.ir_ptr == 14);

#undef CHECK
    return fails;
}

static int test_memory_interface_and_sync_pins()
{
    int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)
    scn2674_t avdc{};
    scn2674_init(&avdc);
    uint64_t pins = SCN2674_CS | SCN2674_RD | SCN2674_WR | SCN2674_RESET |
                    SCN2674_CHAR_LOAD;
    SCN2674_SET_DATA(pins, 0x41);
    pins = scn2674_tick(&avdc, pins);
    CHECK(avdc.char_latch == 0x41);
    pins = (pins & ~SCN2674_CHAR_LOAD) | SCN2674_ATTR_LOAD;
    SCN2674_SET_DATA(pins, 0x87);
    pins = scn2674_tick(&avdc, pins);
    CHECK(avdc.attr_latch == 0x87);
    pins = (pins & ~SCN2674_ATTR_LOAD) | SCN2674_CHAR_OE;
    pins = scn2674_tick(&avdc, pins);
    CHECK(SCN2674_GET_DATA(pins) == 0x41);
    pins = (pins & ~SCN2674_CHAR_OE) | SCN2674_ATTR_OE;
    pins = scn2674_tick(&avdc, pins);
    CHECK(SCN2674_GET_DATA(pins) == 0x87);

    pins &= ~SCN2674_ATTR_OE;
    bool saw_hsync_high = false;
    bool saw_hsync_low = false;
    bool saw_vblank = false;
    for (int i = 0; i < 50000; ++i) {
        pins = scn2674_tick(&avdc, pins);
        saw_hsync_high = saw_hsync_high || ((pins & SCN2674_HSYNC) != 0);
        saw_hsync_low = saw_hsync_low || ((pins & SCN2674_HSYNC) == 0);
        saw_vblank = saw_vblank || ((pins & SCN2674_VBLANK) != 0);
    }
    CHECK(saw_hsync_high);
    CHECK(saw_hsync_low);
    CHECK(saw_vblank);
#undef CHECK
    return fails;
}

}

int main()
{
    int fails = 0;
    fails += test_interrupt_command_groups();
    fails += test_ir_pointer_load_command();
    fails += test_memory_interface_and_sync_pins();
    if (fails == 0) {
        std::puts("OK");
    }
    return fails ? 1 : 0;
}
