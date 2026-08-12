#include "partner.hpp"

#include <initializer_list>
#include <cstdio>

namespace {

class partner_covox_test : public partner
{
public:
    partner_covox_test() : partner("/tmp/idp-test-covox.nvram") {}

    using partner::io_write;
    int observed_pio_writes = 0;
    std::array<uint8_t, 8> observed_ports{};
    std::array<uint8_t, 8> observed_data{};
    std::array<uint8_t, 8> observed_modes{};

    void set_tick(uint64_t value) { tick_count = value; }

    void load_program(std::initializer_list<uint8_t> bytes)
    {
        rom_enabled = false;
        ram_bank = 1;
        size_t offset = 0;
        for (uint8_t byte : bytes)
            ram[offset++] = byte;
    }

    void io_write(uint16_t port, uint8_t data) override
    {
        if ((port & 0xFF) >= 0xD0 && (port & 0xFF) <= 0xD3)
        {
            if (observed_pio_writes < static_cast<int>(observed_ports.size()))
            {
                observed_ports[observed_pio_writes] = static_cast<uint8_t>(port);
                observed_data[observed_pio_writes] = data;
            }
            ++observed_pio_writes;
        }
        const int write_index = observed_pio_writes - 1;
        partner::io_write(port, data);
        if (write_index >= 0 &&
            write_index < static_cast<int>(observed_modes.size()))
            observed_modes[write_index] = pio.port[Z80PIO_PORT_A].mode;
    }
};

} // namespace

int main()
{
    int failures = 0;
#define CHECK(condition) do { \
    if (!(condition)) { \
        std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        ++failures; \
    } \
} while (0)

    partner_covox_test emu;
    partner::pio_device_config covox;
    covox.kind = partner::pio_device_kind::covox;
    emu.set_pio_device_config(partner::pio_port_id::a, covox);

    // A data write while the PIO is still in its reset/input mode is not a
    // physical DAC output and must not become sound.
    emu.io_write(0xD0, 0x11);
    CHECK(emu.drain_covox_sample_events().empty());

    emu.io_write(0xD1, 0x0F); // PIO A mode 0 (output)
    emu.set_tick(1234);
    emu.io_write(0xD0, 0x22);
    emu.set_tick(5678);
    emu.io_write(0xD0, 0xE0);

    auto events = emu.drain_covox_sample_events();
    CHECK(events.size() == 2);
    if (events.size() == 2)
    {
        CHECK(events[0].port == partner::pio_port_id::a);
        CHECK(events[0].tick == 1234);
        CHECK(events[0].sample == 0x22);
        CHECK(events[1].port == partner::pio_port_id::a);
        CHECK(events[1].tick == 5678);
        CHECK(events[1].sample == 0xE0);
    }
    CHECK(emu.drain_covox_sample_events().empty());

    emu.set_pio_device_config(partner::pio_port_id::b, covox);
    emu.io_write(0xD3, 0x0F); // PIO B mode 0 (output)
    emu.set_tick(9000);
    emu.io_write(0xD2, 0x7F);
    events = emu.drain_covox_sample_events();
    CHECK(events.size() == 1);
    if (events.size() == 1)
    {
        CHECK(events[0].port == partner::pio_port_id::b);
        CHECK(events[0].tick == 9000);
        CHECK(events[0].sample == 0x7F);
    }

    // Detaching a port removes samples that have not yet reached the host.
    emu.set_tick(10000);
    emu.io_write(0xD0, 0x44);
    partner::pio_device_config none;
    emu.set_pio_device_config(partner::pio_port_id::a, none);
    CHECK(emu.drain_covox_sample_events().empty());

    // Exercise the real CPU bus path as well as direct diagnostic I/O calls.
    partner_covox_test cpu_emu;
    cpu_emu.set_pio_device_config(partner::pio_port_id::a, covox);
    cpu_emu.load_program({
        0x3E, 0x0F,       // LD A,0Fh
        0xD3, 0xD1,       // OUT (D1h),A: PIO A output mode
        0x3E, 0x20,       // LD A,20h
        0xD3, 0xD0,       // OUT (D0h),A
        0x3E, 0xE0,       // LD A,E0h
        0xD3, 0xD0,       // OUT (D0h),A
        0x76,             // HALT
    });
    for (int i = 0; i < 500; ++i)
        cpu_emu.tick();
    events = cpu_emu.drain_covox_sample_events();
    if (events.size() < 2)
    {
        std::printf("CPU path diagnostic: pc=%04X mode=%u bytes=%llu output=%02X events=%zu writes=%d\n",
                    cpu_emu.get_current_pc(),
                    static_cast<unsigned>(cpu_emu.get_pio().port[Z80PIO_PORT_A].mode),
                    static_cast<unsigned long long>(
                        cpu_emu.get_pio_port_status(partner::pio_port_id::a).bytes_seen),
                    cpu_emu.get_pio().port[Z80PIO_PORT_A].output,
                    events.size(), cpu_emu.observed_pio_writes);
        for (int i = 0; i < cpu_emu.observed_pio_writes; ++i)
            std::printf("  write %02X=%02X mode-after=%u\n",
                        cpu_emu.observed_ports[i], cpu_emu.observed_data[i],
                        static_cast<unsigned>(cpu_emu.observed_modes[i]));
    }
    CHECK(events.size() >= 2);
    if (events.size() >= 2)
    {
        CHECK(events[events.size() - 2].sample == 0x20);
        CHECK(events.back().sample == 0xE0);
    }

#undef CHECK
    if (failures == 0)
    {
        std::puts("test_partner_covox: all tests passed");
        return 0;
    }
    std::printf("test_partner_covox: %d failure(s)\n", failures);
    return 1;
}
