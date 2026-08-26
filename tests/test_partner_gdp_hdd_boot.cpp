#include "partner_gdp.hpp"

#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>

namespace {

constexpr uint64_t BOOT_LIMIT = 240000000ULL;

bool check(bool condition, const char *message)
{
    if (!condition)
        std::printf("test_partner_gdp_hdd_boot: FAIL %s\n", message);
    return condition;
}

bool display_contains(const partner_gdp &emu, std::string_view text)
{
    const auto &vram = emu.get_avdc().vram;
    for (size_t start = 0; start < sizeof(vram); ++start) {
        size_t offset = 0;
        while (offset < text.size() &&
               vram[(start + offset) & 0x3fffu] ==
                   static_cast<uint8_t>(text[offset]))
            ++offset;
        if (offset == text.size())
            return true;
    }
    return false;
}

bool display_cursor_follows_text(const partner_gdp &emu,
                                 std::string_view text)
{
    const auto &avdc = emu.get_avdc();
    for (size_t start = 0; start < sizeof(avdc.vram); ++start) {
        size_t offset = 0;
        while (offset < text.size() &&
               avdc.vram[(start + offset) & 0x3fffu] ==
                   static_cast<uint8_t>(text[offset]))
            ++offset;
        if (offset == text.size() &&
            ((start + text.size()) & 0x3fffu) == avdc.cursor_addr)
            return true;
    }
    return false;
}

void dump_vram_matches(const partner_gdp &emu, std::string_view prefix)
{
    const auto &avdc = emu.get_avdc();
    for (size_t start = 0; start < sizeof(avdc.vram); ++start) {
        size_t offset = 0;
        while (offset < prefix.size() &&
               avdc.vram[(start + offset) & 0x3fffu] ==
                   static_cast<uint8_t>(prefix[offset]))
            ++offset;
        if (offset != prefix.size())
            continue;
        std::printf("test_partner_gdp_hdd_boot: vram[%04zx]=", start);
        for (size_t index = 0; index < 48; ++index) {
            const uint8_t ch = avdc.vram[(start + index) & 0x3fffu];
            std::putchar(ch >= 0x20 && ch < 0x7f ? ch : '.');
        }
        std::printf(" attrs=");
        for (size_t index = 0; index < 24; ++index)
            std::printf("%02x", avdc.attr_vram[(start + index) & 0x3fffu]);
        std::printf(" raw=");
        for (size_t index = 0; index < 24; ++index)
            std::printf("%02x", avdc.vram[(start + index) & 0x3fffu]);
        std::putchar('\n');
    }
}

} // namespace

int main()
{
    namespace fs = std::filesystem;

    const fs::path nvram = fs::path(IDP_SOURCE_ROOT) /
        "tests/dump/partner-gdp-hdd-boot.nvram";
    std::error_code error;
    fs::remove(nvram, error);
    if (!fs::copy_file(
            fs::path(IDP_SOURCE_ROOT) / "partner_cmos.bin", nvram,
            fs::copy_options::overwrite_existing, error)) {
        std::printf("test_partner_gdp_hdd_boot: FAIL could not copy CMOS: %s\n",
                    error.message().c_str());
        return 1;
    }

    bool ok = true;
    {
        partner_gdp emu(terminal_profile::vt100_ansi, nvram.string());
        emu.load_rom(std::string(IDP_SOURCE_ROOT) + "/roms/partner_gdp.rom");
        const char *image_override = std::getenv("IDP_TEST_GDP_HDD");
        emu.load_hdd(image_override != nullptr ? image_override :
            std::string(IDP_SOURCE_ROOT) +
                "/disks/hdd-partner-g-system.img");
        emu.reset();

        bool hard_disk_routine_entered = false;
        bool system_started = false;
        bool prompt_seen = false;
        bool squid_connected = false;
        bool paket_connected = false;
        bool paket_failed = false;
        bool catalog_seen = false;
        bool catalog_id_seen = false;
        bool catalog_total_seen = false;
        bool prompt_cleared = false;
        bool prompt_returned = false;
        uint64_t ef_writes_at_prompt = 0;
        size_t command_position = 0;
        const std::string command = "paket\r";
        uint16_t last_pc = 0;
        while (emu.get_tick_count() < BOOT_LIMIT) {
            emu.tick();
            last_pc = emu.get_current_pc();
            if (last_pc == 0x060B || last_pc == 0x062D)
                hard_disk_routine_entered = true;
            if (!emu.is_rom_enabled())
                system_started = true;

            if ((emu.get_tick_count() % 200000u) == 0u) {
                const std::string output = emu.dump_raw_serial_text();
                const bool prompt_displayed = display_contains(emu, "A>");
                if (!prompt_seen &&
                    (output.find("A>") != std::string::npos ||
                     prompt_displayed)) {
                    prompt_seen = true;
                    ef_writes_at_prompt = emu.get_io_counters().ef_wr;
                }
                if (prompt_seen && command_position == command.size() &&
                    !prompt_displayed)
                    prompt_cleared = true;
                squid_connected = emu.get_sio_port_status(
                    partner::sio_port_id::sio1_b).connected;
                paket_connected = paket_connected ||
                    output.find("povezano.") != std::string::npos ||
                    display_contains(emu, "povezano.");
                paket_failed = paket_failed ||
                    output.find("Napaka:") != std::string::npos ||
                    display_contains(emu, "Napaka:");
                catalog_seen = catalog_seen ||
                    output.find("KATALOG PAKETOV") != std::string::npos ||
                    display_contains(emu, "KATALOG PAKETOV");
                catalog_id_seen = catalog_id_seen ||
                    output.find("kontrabant-2") != std::string::npos ||
                    display_contains(emu, "kontrabant-2");
                catalog_total_seen = catalog_total_seen ||
                    output.find("Skupaj: 10 paketov.") != std::string::npos ||
                    display_contains(emu, "Skupaj: 10 paketov.");
                prompt_returned = catalog_seen && catalog_id_seen &&
                    catalog_total_seen && prompt_cleared &&
                    display_cursor_follows_text(emu, "A>");
            }
            if (prompt_seen && command_position < command.size() &&
                emu.pending_key_count() == 0u)
                emu.key_input(command[command_position++]);
            if (command_position == command.size() &&
                (prompt_returned || paket_failed))
                break;
        }

        ok &= check(hard_disk_routine_entered,
                    "firmware did not select the configured system hard disk");
        ok &= check(emu.get_sasi_data_phase_reads() > 0,
                    "DMA did not read any hard-disk data bytes");
        ok &= check(system_started,
                    "firmware did not start the attached hard-disk system");
        ok &= check(emu.get_rtc().regs[0x0B] == 0x08u,
                    "GDP VT100 profile did not select ANSI/Yugoslav CMOS mode");
        ok &= check(prompt_seen,
                    "hard-disk CP/M did not present a GDP prompt");
        ok &= check(command_position == command.size(),
                    "hard-disk CP/M did not accept a GDP keyboard command");
        ok &= check(squid_connected,
                    "PAKET did not establish its internal Squid serial link");
        ok &= check(paket_connected && !paket_failed,
                    "PAKET did not receive the internal Squid handshake");
        ok &= check(catalog_seen,
                    "GDP PAKET did not display the catalog");
        ok &= check(catalog_id_seen,
                    "GDP PAKET corrupted the kontrabant-2 catalog id");
        ok &= check(catalog_total_seen,
                    "GDP PAKET did not display the catalog total");
        ok &= check(emu.get_io_counters().ef_wr == ef_writes_at_prompt,
                    "ANSI output leaked into the GDP graphics plane");
        ok &= check(prompt_returned,
                    "GDP PAKET did not return the CP/M cursor after output");
        if (!ok) {
            dump_vram_matches(emu, "kontra");
            dump_vram_matches(emu, "Skupaj:");
            const auto &dma = emu.get_dma();
            const auto &hdc = emu.get_hdc();
            const auto squid = emu.get_sio_port_status(
                partner::sio_port_id::sio1_b);
            std::printf(
                "test_partner_gdp_hdd_boot: ticks=%llu pc=%04X rom=%d "
                "sasi_reads=%u phase_reads=%u dma=%d dir_ab=%d "
                "a=%04X/%u/%d b=%04X/%u/%d hdc_phase=%u data=%u/%u "
                "pending_keys=%zu squid_tx=%llu squid_rx=%llu "
                "squid_status=%s\n",
                static_cast<unsigned long long>(emu.get_tick_count()),
                static_cast<unsigned>(last_pc), emu.is_rom_enabled() ? 1 : 0,
                emu.get_sasi_data_reads(), emu.get_sasi_data_phase_reads(),
                dma.enabled ? 1 : 0, dma.direction_ab ? 1 : 0,
                dma.port_a.address, dma.port_a.block_length,
                dma.port_a.is_memory ? 1 : 0,
                dma.port_b.address, dma.port_b.block_length,
                dma.port_b.is_memory ? 1 : 0,
                static_cast<unsigned>(hdc.phase), hdc.data_idx, hdc.data_len,
                emu.pending_key_count(),
                static_cast<unsigned long long>(squid.tx_bytes),
                static_cast<unsigned long long>(squid.rx_bytes),
                squid.detail.c_str());
            std::printf("test_partner_gdp_hdd_boot: output=%s\n",
                        emu.dump_raw_serial_text().c_str());
            std::printf("test_partner_gdp_hdd_boot: cursor=%04x prompt_at=",
                        emu.get_avdc().cursor_addr);
            for (size_t index = 0; index < sizeof(emu.get_avdc().vram); ++index) {
                if (emu.get_avdc().vram[index] == 'A' &&
                    emu.get_avdc().vram[(index + 1u) & 0x3fffu] == '>')
                    std::printf("%04zx,", index);
            }
            std::printf("\n");
        }
    }

    fs::remove(nvram, error);
    if (!ok)
        return 1;
    std::puts("test_partner_gdp_hdd_boot: PASS");
    return 0;
}
