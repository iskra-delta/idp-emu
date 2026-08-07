#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "partner_gdp.hpp"

namespace {

constexpr uint64_t TICK_LIMIT = 4'000'000ULL;
constexpr uint16_t GDP_TEXT_BASE = 0x0100;
constexpr std::array<uint8_t, 8> INVALID_NVRAM = {
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static bool build_partos_rom(const std::filesystem::path &source_root)
{
    const std::string cmd = "make -C " + (source_root / "partos").string() + " -s";
    return std::system(cmd.c_str()) == 0;
}

static bool write_invalid_nvram(const std::filesystem::path &path)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file)
        return false;
    file.write(reinterpret_cast<const char *>(INVALID_NVRAM.data()),
               (std::streamsize)INVALID_NVRAM.size());
    return (bool)file;
}

static bool has_gdp_text(const scn2674_t &avdc, int row, int col, const char *text)
{
    const int stride = std::max(1, (int)avdc.chars_per_row);
    const uint16_t base = (uint16_t)(GDP_TEXT_BASE + row * stride + col);
    for (int i = 0; text[i] != '\0'; ++i) {
        const uint16_t addr = (uint16_t)((base + i) & 0x3FFFu);
        if (avdc.vram[addr] != (uint8_t)text[i])
            return false;
    }
    return true;
}

static bool enter_bios_setup(partner_gdp &emu, bool &saw_banner)
{
    saw_banner = false;
    for (uint64_t ticks = 0; ticks < TICK_LIMIT; ++ticks) {
        if ((ticks & 0x7FFu) == 0)
            emu.key_input(0xfe);
        emu.tick();
        if (!saw_banner && has_gdp_text(emu.get_avdc(), 13, 34, "PARTOS"))
            saw_banner = true;
        if (has_gdp_text(emu.get_avdc(), 10, 22, "KEYBOARD"))
            return true;
    }
    return false;
}

static bool expect_gdp_text(const scn2674_t &avdc,
                            int row,
                            int col,
                            const char *text,
                            const char *label)
{
    const int stride = std::max(1, (int)avdc.chars_per_row);
    const uint16_t base = (uint16_t)(GDP_TEXT_BASE + row * stride + col);
    for (int i = 0; text[i] != '\0'; ++i) {
        const uint16_t addr = (uint16_t)((base + i) & 0x3FFFu);
        const uint8_t got = avdc.vram[addr];
        if (got != (uint8_t)text[i]) {
            std::printf("FAIL %s: row=%d col=%d+i=%d expected %02x got %02x\n",
                        label, row, col, i, (unsigned)(uint8_t)text[i], (unsigned)got);
            return false;
        }
    }
    return true;
}

static bool expect_gdp_attr(const scn2674_t &avdc,
                            int row,
                            int col,
                            const char *text,
                            uint8_t attr,
                            const char *label)
{
    const int stride = std::max(1, (int)avdc.chars_per_row);
    const uint16_t base = (uint16_t)(GDP_TEXT_BASE + row * stride + col);
    for (int i = 0; text[i] != '\0'; ++i) {
        const uint16_t addr = (uint16_t)((base + i) & 0x3FFFu);
        const uint8_t got = avdc.attr_vram[addr];
        if (got != attr) {
            std::printf("FAIL %s: row=%d col=%d+i=%d expected attr %02x got %02x\n",
                        label, row, col, i, (unsigned)attr, (unsigned)got);
            return false;
        }
    }
    return true;
}

static int find_gdp_text_col(const scn2674_t &avdc, int row, const char *text)
{
    const int stride = std::max(1, (int)avdc.chars_per_row);
    for (int col = 0; col <= stride; ++col) {
        bool match = true;
        for (int i = 0; text[i] != '\0'; ++i) {
            if ((col + i) >= stride) {
                match = false;
                break;
            }
            const uint16_t addr =
                (uint16_t)((GDP_TEXT_BASE + row * stride + col + i) & 0x3FFFu);
            if (avdc.vram[addr] != (uint8_t)text[i]) {
                match = false;
                break;
            }
        }
        if (match)
            return col;
    }
    return -1;
}

static void find_gdp_text_anywhere(const scn2674_t &avdc,
                                   const char *text,
                                   int &out_row,
                                   int &out_col)
{
    out_row = -1;
    out_col = -1;
    for (int row = 0; row < 26; ++row) {
        const int col = find_gdp_text_col(avdc, row, text);
        if (col >= 0) {
            out_row = row;
            out_col = col;
            return;
        }
    }
}

static bool gdp_contains_text(const scn2674_t &avdc, const char *text)
{
    int row = -1;
    int col = -1;

    find_gdp_text_anywhere(avdc, text, row, col);
    return row >= 0 && col >= 0;
}

static bool wait_for_gdp_any_text(partner_gdp &emu,
                                  const char *text0,
                                  const char *text1)
{
    for (uint64_t ticks = 0; ticks < TICK_LIMIT; ++ticks) {
        emu.tick();
        if ((ticks & 0x7FFu) != 0)
            continue;
        const auto &avdc = emu.get_avdc();
        if (gdp_contains_text(avdc, text0) ||
            gdp_contains_text(avdc, text1)) {
            return true;
        }
    }
    return false;
}

} // namespace

int main()
{
    int fails = 0;
    namespace fs = std::filesystem;

    const fs::path root = IDP_SOURCE_ROOT;
    const fs::path rom_path = root / "partos" / "bin" / "partos.rom";
    const fs::path tmp_dir = root / "tests" / ".tmp-partos-bios-menu-gdp";
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

    partner_gdp emu(terminal_profile::vt100_ansi, nvram_path.string());
    emu.load_rom(rom_path.string());
    emu.reset();

    bool saw_banner = false;
    if (!enter_bios_setup(emu, saw_banner)) {
        const auto &avdc = emu.get_avdc();
        int row = -1;
        int col = -1;
        find_gdp_text_anywhere(avdc, "B I O S  S E T U P", row, col);
        std::printf("INFO bios title anywhere before fail: row=%d col=%d banner=%d\n",
                    row, col, saw_banner ? 1 : 0);
        find_gdp_text_anywhere(avdc, "KEYBOARD", row, col);
        std::printf("INFO keyboard anywhere before fail: row=%d col=%d\n", row, col);
        std::puts("FAIL BIOS screen did not appear after SETUP");
        return 1;
    }
    if (!saw_banner) {
        std::puts("FAIL GDP banner did not appear");
        return 1;
    }

    const auto &avdc = emu.get_avdc();

    fails += !expect_gdp_text(avdc, 0, 34, "PARTNER GDP", "bios model");
    fails += !expect_gdp_text(avdc, 4, 35, "BIOS SETUP", "bios title");
    fails += !expect_gdp_attr(avdc, 4, 35, "BIOS SETUP", 0x10, "bios title attr");
    fails += !expect_gdp_text(avdc, 8, 8, "SERIAL", "serial title");
    fails += !expect_gdp_attr(avdc, 8, 8, "SERIAL", 0x10, "serial title attr");
    fails += !expect_gdp_text(avdc, 10, 8, "TTYS0", "ttys0 label");
    fails += !expect_gdp_text(avdc, 10, 22, "KEYBOARD", "ttys0 value");
    fails += !expect_gdp_attr(avdc, 10, 22, "KEYBOARD", 0x30, "ttys0 value attr");

    if (fails != 0) {
        int row = -1;
        int col = -1;
        find_gdp_text_anywhere(avdc, "BIOS SETUP", row, col);
        std::printf("INFO bios title anywhere: row=%d col=%d\n", row, col);
        find_gdp_text_anywhere(avdc, "SERIAL", row, col);
        std::printf("INFO serial title anywhere: row=%d col=%d\n", row, col);
        find_gdp_text_anywhere(avdc, "TTYS0", row, col);
        std::printf("INFO ttys0 anywhere: row=%d col=%d\n", row, col);
        find_gdp_text_anywhere(avdc, "KEYBOARD", row, col);
        std::printf("INFO keyboard anywhere: row=%d col=%d\n", row, col);
    }

    emu.key_input(0x03);
    if (!wait_for_gdp_any_text(emu, "BOOT...", "NO BOOT")) {
        std::puts("FAIL boot path did not restart after Ctrl+C");
        ++fails;
    }
    const auto &post_exit = emu.get_avdc();
    if (gdp_contains_text(post_exit, "BIOS SETUP")) {
        std::puts("FAIL setup title remained visible after boot restart");
        ++fails;
    }
    if (gdp_contains_text(post_exit, "KEYBOARD")) {
        std::puts("FAIL setup values remained visible after boot restart");
        ++fails;
    }
    if (gdp_contains_text(post_exit, "FLOPPY")) {
        std::puts("FAIL setup sections remained visible after boot restart");
        ++fails;
    }

    fs::remove(nvram_path, ec);
    fs::remove(tmp_dir, ec);

    if (fails == 0) {
        std::puts("test_partos_bios_menu_gdp: PASS");
        return 0;
    }

    std::printf("test_partos_bios_menu_gdp: %d failure(s)\n", fails);
    return 1;
}
