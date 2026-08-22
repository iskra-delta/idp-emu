#include "partner_crt.hpp"

#include <cstdint>
#include <cstdio>
#include <string>

namespace {

constexpr uint64_t BOOT_LIMIT = 60000000ULL;

bool boot_prompt_wait(uint16_t pc)
{
    return pc == 0x009F || pc == 0x00A1 || pc == 0x00A3;
}

bool check(bool condition, const char *message)
{
    if (!condition)
        std::printf("test_partner_crt_boot: FAIL %s\n", message);
    return condition;
}

} // namespace

int main()
{
    partner_crt emu(terminal_profile::vt52, "");
    emu.load_rom(std::string(IDP_SOURCE_ROOT) + "/roms/partner_crt.rom");
    emu.load_disk(0, std::string(IDP_SOURCE_ROOT) + "/disks/fdd-partner-p.img");
    if (!check(emu.is_rom_enabled(), "could not load the Partner CRT ROM"))
        return 1;
    emu.reset();

    bool key_sent = false;
    bool floppy_routine_entered = false;
    bool floppy_boot_started = false;
    uint16_t last_pc = 0;
    while (emu.get_tick_count() < BOOT_LIMIT) {
        emu.tick();
        last_pc = emu.get_current_pc();
        if (!key_sent && boot_prompt_wait(last_pc)) {
            emu.key_input('f');
            key_sent = true;
        }
        if (last_pc == 0x020F || last_pc == 0x03F5 || last_pc == 0x03A5 ||
            last_pc == 0x0292)
            floppy_routine_entered = true;
        if (key_sent && !emu.is_rom_enabled()) {
            floppy_boot_started = true;
            break;
        }
    }

    bool ok = true;
    ok &= check(key_sent, "ROM never reached the firmware boot prompt");
    ok &= check(floppy_routine_entered,
                "firmware did not accept F as the floppy selection");
    ok &= check(floppy_boot_started,
                "firmware did not start the attached system floppy after F");
    if (!ok) {
        const auto &dma = emu.get_dma();
        const auto &fdc = emu.get_fdc();
        std::printf("test_partner_crt_boot: ticks=%llu pc=%04X rom=%d fdc_reads=%llu\n",
                    static_cast<unsigned long long>(emu.get_tick_count()),
                    static_cast<unsigned>(last_pc), emu.is_rom_enabled() ? 1 : 0,
                    static_cast<unsigned long long>(emu.get_dma_fdc_reads()));
        std::printf("test_partner_crt_boot: dma_direction_ab=%d state=%u enabled=%d "
                    "a=%04X/%u/%d b=%04X/%u/%d fdc_phase=%u data=%u/%u\n",
                    dma.direction_ab ? 1 : 0, static_cast<unsigned>(dma.state),
                    dma.enabled ? 1 : 0, dma.port_a.address,
                    dma.port_a.block_length, dma.port_a.is_memory ? 1 : 0,
                    dma.port_b.address, dma.port_b.block_length,
                    dma.port_b.is_memory ? 1 : 0, static_cast<unsigned>(fdc.phase),
                    fdc.data_idx, fdc.data_len);
        std::printf("test_partner_crt_boot: serial=%s\n",
                    emu.dump_raw_serial_text().c_str());
        return 1;
    }

    std::puts("test_partner_crt_boot: PASS");
    return 0;
}
