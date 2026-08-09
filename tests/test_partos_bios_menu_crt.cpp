#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "gui/display.hpp"
#include "partner_crt.hpp"

namespace {

constexpr uint64_t TICK_LIMIT = 4'000'000ULL;
constexpr int CELL_W = display::CHAR_W;
constexpr int CELL_H = display::CHAR_H;
constexpr std::array<uint8_t, 8> INVALID_NVRAM = {
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

bool build_partos_rom(const std::filesystem::path &source_root)
{
    const std::string cmd = "make -C " PARTOS_ROOT " -s";
    return std::system(cmd.c_str()) == 0;
}

bool write_invalid_nvram(const std::filesystem::path &path)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file)
        return false;
    file.write(reinterpret_cast<const char *>(INVALID_NVRAM.data()),
               (std::streamsize)INVALID_NVRAM.size());
    return (bool)file;
}

bool wait_for_any_text(partner_crt &emu,
                       const std::string &a,
                       const std::string &b)
{
    for (uint64_t ticks = 0; ticks < TICK_LIMIT; ++ticks) {
        emu.tick();
        const std::string text = emu.dump_terminal_text();
        if ((text.find(a) != std::string::npos) ||
            (text.find(b) != std::string::npos))
            return true;
    }
    return false;
}

bool enter_bios_setup(partner_crt &emu)
{
    for (uint64_t ticks = 0; ticks < TICK_LIMIT; ++ticks) {
        if ((ticks & 0x7FFu) == 0)
            emu.key_input(0xfe);
        emu.tick();
        const std::string text = emu.dump_terminal_text();
        if ((text.find("BIOS SETUP") != std::string::npos) &&
            (text.find("KEYBOARD") != std::string::npos))
            return true;
    }
    return false;
}

int cell_max(const display &disp, int col, int row)
{
    int result = 0;
    const uint8_t *fb = disp.data();
    const int x0 = col * CELL_W;
    const int y0 = row * CELL_H;
    for (int y = y0; y < y0 + CELL_H; ++y) {
        for (int x = x0; x < x0 + CELL_W; ++x)
            result = std::max<int>(result, fb[x + y * display::FB_W]);
    }
    return result;
}

int cell_sum(const display &disp, int col, int row)
{
    int result = 0;
    const uint8_t *fb = disp.data();
    const int x0 = col * CELL_W;
    const int y0 = row * CELL_H;
    for (int y = y0; y < y0 + CELL_H; ++y) {
        for (int x = x0; x < x0 + CELL_W; ++x)
            result += fb[x + y * display::FB_W];
    }
    return result;
}

uint8_t sample_cell_pixel(const display &disp, int col, int row, int xoff, int yoff)
{
    const uint8_t *fb = disp.data();
    const int x = col * CELL_W + xoff;
    const int y = row * CELL_H + yoff;
    return fb[x + y * display::FB_W];
}

bool expect(bool cond, const char *label)
{
    if (!cond) {
        std::printf("FAIL %s\n", label);
        return false;
    }
    return true;
}

} // namespace

int main()
{
    int fails = 0;
    namespace fs = std::filesystem;

    const fs::path root = IDP_SOURCE_ROOT;
    const fs::path rom_path = PARTOS_ROOT "/bin/partos.rom";
    const fs::path tmp_dir = root / "tests" / ".tmp-partos-bios-menu-crt";
    const fs::path nvram_path = tmp_dir / "invalid-nvram.bin";

    if (!build_partos_rom(root)) {
        std::puts("FAIL partos build failed");
        return 1;
    }

    std::error_code ec;
    fs::create_directories(tmp_dir, ec);
    if (!write_invalid_nvram(nvram_path)) {
        std::puts("FAIL could not create invalid NVRAM image");
        return 1;
    }

    partner_crt emu(terminal_profile::vt52, nvram_path.string());
    emu.load_rom(rom_path.string());
    emu.reset();

    if (!enter_bios_setup(emu)) {
        const auto cpu = emu.capture_debug_cpu_state();
        std::printf("FAIL BIOS setup did not appear on CRT (pc=%04x sp=%04x halted=%d)\n",
                    (unsigned)cpu.pc, (unsigned)cpu.sp, cpu.halted ? 1 : 0);
        std::puts("INFO terminal dump follows:");
        std::puts(emu.dump_terminal_text().c_str());
        return 1;
    }

    display disp;
    disp.load_font("");
    disp.set_phosphor_type(display::phosphor_type::flat);
    emu.render_to(disp);

    fails += !expect(cell_max(disp, 35, 4) == 232, "bios title renders highlighted");
    fails += !expect(cell_max(disp, 8, 10) == 168, "menu labels render normal");
    fails += !expect(cell_sum(disp, 22, 10) > cell_sum(disp, 22, 11) * 4,
                     "selected value renders inverse background");
    fails += !expect(sample_cell_pixel(disp, 22, 10, CELL_W - 1, 0) == 168,
                     "inverse cell fills through gap column");
    fails += !expect(sample_cell_pixel(disp, 22, 11, CELL_W - 1, 0) == 0,
                     "normal cell keeps gap column dark");
    fails += !expect(cell_max(disp, 30, 10) == 0, "bios setup keeps cursor hidden");

    if (fails != 0) {
        std::printf("INFO title max=%d label max=%d selected sum=%d unselected sum=%d inv_gap=%u norm_gap=%u\n",
                    cell_max(disp, 35, 4),
                    cell_max(disp, 8, 10),
                    cell_sum(disp, 22, 10),
                    cell_sum(disp, 22, 11),
                    (unsigned)sample_cell_pixel(disp, 22, 10, CELL_W - 1, 0),
                    (unsigned)sample_cell_pixel(disp, 22, 11, CELL_W - 1, 0));
        std::puts("INFO setup terminal dump follows:");
        std::puts(emu.dump_terminal_text().c_str());
    }

    emu.key_input(0x03);
    if (!wait_for_any_text(emu, "BOOT...", "NO BOOT")) {
        const auto cpu = emu.capture_debug_cpu_state();
        std::printf("FAIL boot path did not restart after Ctrl+C (pc=%04x sp=%04x halted=%d)\n",
                    (unsigned)cpu.pc, (unsigned)cpu.sp, cpu.halted ? 1 : 0);
        std::puts("INFO post-exit terminal dump follows:");
        std::puts(emu.dump_terminal_text().c_str());
        return 1;
    }

    const std::string post_exit = emu.dump_terminal_text();
    fails += !expect(post_exit.find("BIOS SETUP") == std::string::npos,
                     "setup title cleared before boot restart");
    fails += !expect(post_exit.find("KEYBOARD") == std::string::npos,
                     "setup values cleared before boot restart");
    fails += !expect(post_exit.find("FLOPPY") == std::string::npos,
                     "setup sections cleared before boot restart");

    fs::remove(nvram_path, ec);
    fs::remove(tmp_dir, ec);

    if (fails == 0) {
        std::puts("test_partos_bios_menu_crt: PASS");
        return 0;
    }

    std::printf("test_partos_bios_menu_crt: %d failure(s)\n", fails);
    return 1;
}
