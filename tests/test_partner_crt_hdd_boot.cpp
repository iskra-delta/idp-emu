#include "partner_crt.hpp"

#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>

namespace {

constexpr uint64_t BOOT_LIMIT = 240000000ULL;

bool check(bool condition, const char *message)
{
    if (!condition)
        std::printf("test_partner_crt_hdd_boot: FAIL %s\n", message);
    return condition;
}

} // namespace

int main()
{
    namespace fs = std::filesystem;

    const fs::path nvram = fs::temp_directory_path() /
        "idp-emu-crt-hdd-boot-nvram.bin";
    std::error_code error;
    fs::remove(nvram, error);
    if (!fs::copy_file(
            fs::path(IDP_SOURCE_ROOT) / "partner_cmos.bin", nvram,
            fs::copy_options::overwrite_existing, error)) {
        std::printf("test_partner_crt_hdd_boot: FAIL could not copy CMOS: %s\n",
                    error.message().c_str());
        return 1;
    }

    bool ok = true;
    {
        partner_crt emu(terminal_profile::vt52, nvram.string());
        emu.load_rom(std::string(IDP_SOURCE_ROOT) + "/roms/partner_crt.rom");
        const char *image_override = std::getenv("IDP_TEST_CRT_HDD");
        emu.load_hdd(image_override != nullptr ? image_override :
            std::string(IDP_SOURCE_ROOT) +
                "/disks/hdd-partner-p-system.img");
        emu.reset();

        bool hard_disk_routine_entered = false;
        bool prompt_seen = false;
        bool squid_connected = false;
        bool paket_connected = false;
        bool paket_failed = false;
        bool empty_catalog_seen = false;
        size_t command_position = 0;
        const std::string command = "paket\r";
        while (emu.get_tick_count() < BOOT_LIMIT) {
            emu.tick();
            const uint16_t pc = emu.get_current_pc();
            if (pc == 0x060B || pc == 0x062D)
                hard_disk_routine_entered = true;

            const std::string serial = emu.dump_raw_serial_text();
            if (!prompt_seen && serial.find("A>") != std::string::npos)
                prompt_seen = true;
            if (prompt_seen && command_position < command.size() &&
                emu.pending_key_count() == 0u)
                emu.key_input(command[command_position++]);
            squid_connected = emu.get_sio_port_status(
                partner::sio_port_id::sio1_b).connected;
            paket_connected = serial.find("povezano.") != std::string::npos;
            paket_failed = serial.find("Napaka:") != std::string::npos;
            empty_catalog_seen = serial.find(
                "Ni paketov, ki ustrezajo zahtevi.") != std::string::npos;
            if (command_position == command.size() &&
                (empty_catalog_seen || paket_failed))
                break;
        }

        const std::string serial = emu.dump_raw_serial_text();
        ok &= check(hard_disk_routine_entered,
                    "firmware did not select the configured system hard disk");
        ok &= check(emu.get_sasi_data_phase_reads() > 0,
                    "DMA did not read any hard-disk data bytes");
        ok &= check(!emu.is_rom_enabled(),
                    "firmware did not start the attached hard-disk system");
        ok &= check(prompt_seen,
                    "hard-disk CP/M did not present a prompt on the CRT");
        ok &= check(command_position == command.size(),
                    "hard-disk CP/M did not accept a CRT keyboard command");
        ok &= check(squid_connected,
                    "PAKET did not establish its internal Squid serial link");
        ok &= check(paket_connected && !paket_failed,
                    "PAKET did not receive the internal Squid handshake");
        ok &= check(empty_catalog_seen,
                    "CRT PAKET catalog was not empty for model p");
        if (!ok) {
            const auto squid = emu.get_sio_port_status(
                partner::sio_port_id::sio1_b);
            std::printf(
                "test_partner_crt_hdd_boot: ticks=%llu pc=%04X rom=%d "
                "sasi_reads=%u phase_reads=%u pending_keys=%zu "
                "squid_tx=%llu squid_rx=%llu squid_status=%s\n",
                static_cast<unsigned long long>(emu.get_tick_count()),
                static_cast<unsigned>(emu.get_current_pc()),
                emu.is_rom_enabled() ? 1 : 0, emu.get_sasi_data_reads(),
                emu.get_sasi_data_phase_reads(), emu.pending_key_count(),
                static_cast<unsigned long long>(squid.tx_bytes),
                static_cast<unsigned long long>(squid.rx_bytes),
                squid.detail.c_str());
            std::printf("test_partner_crt_hdd_boot: serial=%s\n", serial.c_str());
        }
    }

    fs::remove(nvram, error);
    if (!ok)
        return 1;
    std::puts("test_partner_crt_hdd_boot: PASS");
    return 0;
}
