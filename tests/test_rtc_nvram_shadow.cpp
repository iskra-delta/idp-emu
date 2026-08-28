#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "partner.hpp"

namespace {

class partner_test : public partner {
public:
    using partner::partner;
    using partner::io_read;
    using partner::io_write;

    void set_emulated_tick(uint64_t value)
    {
        rtc.det_base = 1700000000;
        rtc.det_hz = 4000000u;
        rtc.det_ticks = &tick_count;
        tick_count = value;
        mm58167a_sync_time(&rtc);
    }

    void arm_tenth_second_interrupt()
    {
        tick_count = 0;
        rtc.det_base = 1700000000;
        rtc.det_hz = 4000000u;
        rtc.det_ticks = &tick_count;
        rtc.last_sync_time = 0;
        rtc.last_sync_millisecond = UINT16_MAX;
        mm58167a_sync_time(&rtc);
        for (uint8_t i = 0; i < 8; ++i)
            rtc.regs[0x08 + i] = 0xCC; // comparator wildcard
        io_write(0xB1, 0x02); // register 11h: tenth-second interrupt enable
    }

    void jump_before_tenth_second()
    {
        tick_count = 399999;
    }
};

static int fail(const char *msg)
{
    std::printf("FAIL %s\n", msg);
    return 1;
}

static bool same_visible_rtc_state(const mm58167a_t &a,
                                   const mm58167a_t &b)
{
    return std::equal(std::begin(a.regs), std::end(a.regs), std::begin(b.regs)) &&
        a.addr_latch == b.addr_latch &&
        a.last_sync_time == b.last_sync_time &&
        a.last_sync_millisecond == b.last_sync_millisecond &&
        a.interrupt_status == b.interrupt_status &&
        a.interrupt_control == b.interrupt_control &&
        a.rollover_status == b.rollover_status &&
        a.standby_interrupt_enable == b.standby_interrupt_enable;
}

static bool rtc_idle_tick_lockstep()
{
    mm58167a_t generic{};
    mm58167a_t scheduled{};
    uint64_t generic_tick = 0;
    uint64_t scheduled_tick = 0;
    mm58167a_init(&generic);
    mm58167a_init(&scheduled);
    generic.det_base = scheduled.det_base = 1700000000;
    generic.det_hz = scheduled.det_hz = 4000000u;
    generic.det_ticks = &generic_tick;
    scheduled.det_ticks = &scheduled_tick;
    generic.last_sync_time = scheduled.last_sync_time = 0;
    generic.last_sync_millisecond = scheduled.last_sync_millisecond = UINT16_MAX;
    mm58167a_sync_time(&generic);
    mm58167a_sync_time(&scheduled);
    for (uint8_t i = 0; i < 8; ++i)
        generic.regs[0x08u + i] = scheduled.regs[0x08u + i] = 0xCCu;
    generic.interrupt_control = scheduled.interrupt_control = 0x02u;

    const uint64_t idle = MM58167A_CS | MM58167A_AS | MM58167A_DS |
                          MM58167A_RW | MM58167A_RESET;
    uint64_t generic_pins = idle;
    uint64_t scheduled_pins = idle;
    for (uint64_t tick = 1; tick <= 410000u; ++tick) {
        generic_tick = scheduled_tick = tick;
        generic_pins = mm58167a_tick(&generic, generic_pins);
        scheduled_pins = mm58167a_tick_idle(
            &scheduled, scheduled_pins, (tick % 4000u) == 0u);
        if ((tick % 997u) == 0u || (tick % 4000u) == 0u) {
            if (generic_pins != scheduled_pins ||
                !same_visible_rtc_state(generic, scheduled))
                return false;
        }
    }
    return generic_pins == scheduled_pins &&
        same_visible_rtc_state(generic, scheduled);
}

} // namespace

int main()
{
    namespace fs = std::filesystem;

    if (!rtc_idle_tick_lockstep())
        return fail("scheduled RTC idle tick diverged from per-tick sync");

    const fs::path tmp_dir = fs::path(IDP_SOURCE_ROOT) / "tests/dump/rtc-nvram-shadow";
    const fs::path nvram_path = tmp_dir / "shadow.bin";
    const std::array<uint8_t, 8> defaults = {
        0xF0, 0x98, 0xFF, 0x01, 0x85, 0x07, 0x00, 0x57
    };
    const std::array<uint8_t, 8> updated = {
        0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0
    };

    std::error_code ec;
    fs::create_directories(tmp_dir, ec);
    fs::remove(nvram_path, ec);

    {
        partner_test emu(nvram_path.string());
        emu.set_emulated_tick(492000); // 123 ms at 4 MHz
        if (emu.io_read(0xA1) != 0x12)
            return fail("RTC hundredths do not follow emulated time");
        if (emu.io_read(0xA0) != 0x30)
            return fail("RTC thousandths do not follow emulated time");

        std::ifstream file(nvram_path, std::ios::binary);
        if (!file)
            return fail("shadow NVRAM file was not created");

        std::array<uint8_t, 8> disk{};
        file.read(reinterpret_cast<char *>(disk.data()), (std::streamsize)disk.size());
        if (file.gcount() != (std::streamsize)disk.size())
            return fail("shadow NVRAM file size is not 8 bytes");
        if (disk != defaults)
            return fail("shadow NVRAM defaults do not match seeded Partner bytes");

        for (size_t i = 0; i < updated.size(); ++i)
            emu.io_write((uint16_t)(0xA8 + i), updated[i]);
    }

    {
        const fs::path irq_nvram = tmp_dir / "interrupt.bin";
        partner_test emu(irq_nvram.string());
        emu.arm_tenth_second_interrupt();
        emu.jump_before_tenth_second();
        emu.tick();
        if ((emu.get_pins() & Z80_NMI) != 0)
            return fail("open JJ12 unexpectedly routed RTC interrupt to NMI");
        if ((emu.io_read(0xB0) & 0x02u) == 0u)
            return fail("RTC interrupt status did not latch with JJ12 open");

        emu.set_rtc_nmi_enabled(true);
        emu.arm_tenth_second_interrupt();
        emu.jump_before_tenth_second();
        emu.tick();
        if ((emu.get_pins() & Z80_NMI) == 0)
            return fail("RTC interrupt did not reach CPU NMI through closed JJ12");
        if ((emu.io_read(0xB0) & 0x02u) == 0u)
            return fail("RTC interrupt status did not report the tenth-second source");
        emu.tick();
        if ((emu.get_pins() & Z80_NMI) != 0)
            return fail("RTC status read did not release CPU NMI");
        fs::remove(irq_nvram, ec);
    }

    {
        std::ifstream file(nvram_path, std::ios::binary);
        if (!file)
            return fail("shadow NVRAM file disappeared after writes");

        std::array<uint8_t, 8> disk{};
        file.read(reinterpret_cast<char *>(disk.data()), (std::streamsize)disk.size());
        if (file.gcount() != (std::streamsize)disk.size())
            return fail("shadow NVRAM file size changed after writes");
        if (disk != updated)
            return fail("shadow NVRAM file did not persist written bytes");
    }

    {
        partner_test emu(nvram_path.string());
        for (size_t i = 0; i < updated.size(); ++i) {
            const uint8_t value = emu.io_read((uint16_t)(0xA8 + i));
            if (value != updated[i])
                return fail("shadow NVRAM contents were not restored on reload");
        }
    }

    fs::remove(nvram_path, ec);
    fs::remove(tmp_dir, ec);
    std::puts("PASS rtc_nvram_shadow");
    return 0;
}
