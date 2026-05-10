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

}

int main()
{
    int fails = 0;
    fails += test_interrupt_command_groups();
    fails += test_ir_pointer_load_command();
    if (fails == 0) {
        std::puts("OK");
    }
    return fails ? 1 : 0;
}
