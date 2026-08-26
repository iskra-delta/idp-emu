#include "partner_gdp.hpp"
#include "gui/display.hpp"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>

namespace {

constexpr uint64_t BANNER_TICKS = 8000000ULL;
constexpr uint64_t BOOT_INPUT_LIMIT = 30000000ULL;

bool boot_prompt_wait(uint16_t pc)
{
    return pc == 0x009F || pc == 0x00A1 || pc == 0x00A3;
}

size_t lit_pixels(const display &disp, int first_row, int last_row)
{
    const uint8_t *pixels = disp.data();
    size_t lit = 0;
    for (int y = first_row; y < last_row; ++y) {
        for (int x = 0; x < display::FB_W; ++x) {
            if (pixels[y * display::FB_W + x] != 0)
                ++lit;
        }
    }
    return lit;
}

bool check(bool condition, const char *message)
{
    if (!condition)
        std::printf("test_partner_gdp_boot: FAIL %s\n", message);
    return condition;
}

} // namespace

int main()
{
    partner_gdp emu(terminal_profile::vt100_ansi, "");
    emu.load_rom(std::string(IDP_SOURCE_ROOT) + "/roms/partner_gdp.rom");
    emu.load_disk(0,
        std::string(IDP_SOURCE_ROOT) + "/disks/fdd-partner-g.img");
    if (!check(emu.is_rom_enabled(),
               "could not load the real Partner GDP ROM"))
        return 1;
    emu.reset();

    while (emu.get_tick_count() < BANNER_TICKS)
        emu.tick();

    const std::string banner = emu.dump_raw_serial_text();
    bool ok = true;
    ok &= check(banner.find("Delta Partner GDP") != std::string::npos,
                "ROM did not emit the Partner banner");
    ok &= check(banner.find("TESTING MEMORY") != std::string::npos,
                "ROM did not reach its memory test");
    ok &= check(emu.get_ef9367().y == 0u,
                "EF9367 command 05 did not reset both address registers");

    auto frame = std::make_unique<display>();
    emu.render_to(*frame);
    const size_t banner_band = lit_pixels(*frame, 420, display::FB_H);
    const size_t total_pixels = lit_pixels(*frame, 0, display::FB_H);
    ok &= check(banner_band > 20000u,
                "rendered banner is absent from its ROM-defined screen band");
    ok &= check(total_pixels >= banner_band,
                "rendered banner pixel accounting is inconsistent");

    bool key_sent = false;
    bool key_consumed = false;
    bool floppy_routine_entered = false;
    bool floppy_boot_started = false;
    while (emu.get_tick_count() < BOOT_INPUT_LIMIT) {
        emu.tick();
        const uint16_t pc = emu.get_current_pc();
        if (!key_sent && boot_prompt_wait(pc)) {
            key_sent = emu.key_input('f');
        }
        if (key_sent &&
            emu.dump_raw_serial_text().find("*f") != std::string::npos) {
            key_consumed = true;
        }
        if (pc == 0x020F || pc == 0x03F5 || pc == 0x03A5 || pc == 0x0292)
            floppy_routine_entered = true;
        if (key_sent && !emu.is_rom_enabled()) {
            floppy_boot_started = true;
            break;
        }
    }
    ok &= check(key_sent, "ROM never reached the keyboard boot prompt");
    ok &= check(key_consumed,
                "ROM did not consume GUI-style input through SIO channel A");
    ok &= check(floppy_routine_entered,
                "firmware did not accept F as the floppy selection");
    ok &= check(floppy_boot_started,
                "firmware did not start the attached system floppy after F");

    if (!ok)
        return 1;
    std::puts("test_partner_gdp_boot: PASS");
    return 0;
}
