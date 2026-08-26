#include "partner.hpp"

#include <cstdio>

class partner_external_im2_test : public partner
{
public:
    partner_external_im2_test() : partner(
        std::string(IDP_SOURCE_ROOT) + "/tests/dump/partner-external-im2.nvram") {}

    void set_external_vector(int vector)
    {
        external_vector_ = vector;
    }

    int pending_external_vector() const
    {
        return external_im2_pending_vector_;
    }

    void enter_original_floppy_window()
    {
        cpu.i = 0xFA;
        fdc.phase = I8272_PHASE_COMMAND;
    }

    uint8_t acknowledge()
    {
        uint64_t bus = Z80_IORQ | Z80_M1;
        service_cpu_bus(bus);
        return Z80_GET_DATA(bus);
    }

    void finish_acknowledge()
    {
        uint64_t bus = 0;
        service_cpu_bus(bus);
    }

protected:
    int get_external_im2_vector() const override
    {
        return external_vector_;
    }

private:
    int external_vector_ = -1;
};

int main()
{
    int failures = 0;
#define CHECK(condition) do { \
    if (!(condition)) { \
        std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        ++failures; \
    } \
} while (0)

    partner_external_im2_test emu;
    emu.enter_original_floppy_window();

    // A short board-level pulse must remain pending after its live level has
    // disappeared; the Z80 can take longer than the pulse to reach IM2's data
    // sampling cycle.
    emu.set_external_vector(0x8E);
    emu.tick();
    CHECK(emu.pending_external_vector() == 0x8E);
    CHECK((emu.get_pins() & Z80_INT) != 0);

    emu.set_external_vector(-1);
    for (int i = 0; i < 128; ++i)
        emu.tick();
    CHECK(emu.pending_external_vector() == 0x8E);
    CHECK((emu.get_pins() & Z80_INT) != 0);
    CHECK(emu.acknowledge() == 0x8E);
    CHECK(emu.pending_external_vector() == -1);
    emu.finish_acknowledge();

    // A still-high source is one edge, not a stream of repeated interrupts.
    emu.set_external_vector(0x8E);
    emu.tick();
    CHECK(emu.pending_external_vector() == 0x8E);
    CHECK(emu.acknowledge() == 0x8E);
    emu.finish_acknowledge();
    emu.tick();
    CHECK(emu.pending_external_vector() == -1);
    CHECK((emu.get_pins() & Z80_INT) == 0);
    // A second acknowledge while the same source level is high must see only
    // the spurious-interrupt sink, never another copy of the VBL vector.
    CHECK(emu.acknowledge() == 0x00);
    emu.finish_acknowledge();

    // Going low rearms the latch for the next vertical-blank edge.
    emu.set_external_vector(-1);
    emu.tick();
    emu.set_external_vector(0x8E);
    emu.tick();
    CHECK(emu.pending_external_vector() == 0x8E);

#undef CHECK
    if (failures == 0) {
        std::puts("test_partner_external_im2: all tests passed");
        return 0;
    }
    std::printf("test_partner_external_im2: %d failure(s)\n", failures);
    return 1;
}
