#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "partner_crt.hpp"
#include "partner_gdp.hpp"

namespace {

constexpr uint64_t TICK_LIMIT = 4'000'000ULL;
constexpr int CRT_BANNER_ROW = 12;
constexpr int CRT_BANNER_COL = 34;
constexpr int GDP_BANNER_ROW = 13;
constexpr int GDP_BANNER_COL = 34;
constexpr int GDP_EXPECTED_COLS = 80;
constexpr uint8_t GDP_EXPECTED_TEXT_CTL = 0x6d;
constexpr uint16_t GDP_TEXT_BASE = 0x0100;
constexpr std::array<uint8_t, 8> INVALID_NVRAM = {
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static bool build_partos_rom(const std::filesystem::path &source_root)
{
    const std::string cmd = "make -C " + (source_root / "partos").string() + " -s";
    return std::system(cmd.c_str()) == 0;
}

static bool has_line_fragment(const std::vector<std::string> &lines,
                              size_t row,
                              size_t col,
                              const std::string &fragment)
{
    if (row >= lines.size())
        return false;
    const std::string &line = lines[row];
    if (line.size() < col + fragment.size())
        return false;
    return line.compare(col, fragment.size(), fragment) == 0;
}

static std::vector<std::string> split_lines(const std::string &text)
{
    std::vector<std::string> lines;
    std::string current;
    for (char ch : text) {
        if (ch == '\n') {
            lines.push_back(current);
            current.clear();
        } else {
            current.push_back(ch);
        }
    }
    if (!current.empty())
        lines.push_back(current);
    return lines;
}

static bool expect_line_fragment(const std::vector<std::string> &lines,
                                 size_t row,
                                 size_t col,
                                 const std::string &fragment,
                                 const char *label)
{
    if (row >= lines.size()) {
        std::printf("FAIL %s: missing row %zu\n", label, row);
        return false;
    }
    const std::string &line = lines[row];
    if (line.size() < col + fragment.size()) {
        std::printf("FAIL %s: row %zu too short, need %zu chars and got %zu\n",
                    label, row, col + fragment.size(), line.size());
        return false;
    }
    if (line.compare(col, fragment.size(), fragment) != 0) {
        std::printf("FAIL %s: row %zu col %zu expected \"%s\" got \"%s\"\n",
                    label, row, col, fragment.c_str(), line.c_str());
        return false;
    }
    return true;
}

static bool has_gdp_text(const scn2674_t &avdc,
                         int row,
                         int col,
                         const char *text)
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

static bool wait_for_crt_banner(partner_crt &emu,
                                size_t row,
                                size_t col,
                                const std::string &fragment)
{
    for (uint64_t ticks = 0; ticks < TICK_LIMIT; ++ticks) {
        emu.tick();
        if (has_line_fragment(split_lines(emu.dump_terminal_text()), row, col, fragment))
            return true;
    }
    return false;
}

static bool wait_for_gdp_banner(partner_gdp &emu,
                                int row,
                                int col,
                                const char *text)
{
    for (uint64_t ticks = 0; ticks < TICK_LIMIT; ++ticks) {
        emu.tick();
        if (has_gdp_text(emu.get_avdc(), row, col, text))
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

static int find_gdp_banner_col(const scn2674_t &avdc, int row, const char *text)
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

static void find_gdp_banner_anywhere(const scn2674_t &avdc,
                                     const char *text,
                                     int &out_row,
                                     int &out_col)
{
    out_row = -1;
    out_col = -1;
    for (int row = 0; row < 26; ++row) {
        const int col = find_gdp_banner_col(avdc, row, text);
        if (col >= 0) {
            out_row = row;
            out_col = col;
            return;
        }
    }
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

} // namespace

int main()
{
    int fails = 0;
    namespace fs = std::filesystem;

    const fs::path root = IDP_SOURCE_ROOT;
    const fs::path rom_path = root / "partos" / "bin" / "partos.rom";
    const fs::path tmp_dir = root / "tests" / ".tmp-partos-boot-banner";
    const fs::path nvram_path = tmp_dir / "invalid-nvram.bin";
    const std::string banner = "P A R T O S";

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

    {
        partner_crt emu(terminal_profile::vt52, nvram_path.string());
        emu.load_rom(rom_path.string());
        emu.reset();

        if (!wait_for_crt_banner(emu, CRT_BANNER_ROW, CRT_BANNER_COL, banner)) {
            const auto cpu = emu.capture_debug_cpu_state();
            std::printf("FAIL crt banner did not appear (pc=%04x sp=%04x halted=%d)\n",
                        (unsigned)cpu.pc, (unsigned)cpu.sp, cpu.halted ? 1 : 0);
            ++fails;
        } else {
            const auto lines = split_lines(emu.dump_terminal_text());
            fails += !expect_line_fragment(lines,
                                           CRT_BANNER_ROW,
                                           CRT_BANNER_COL,
                                           banner,
                                           "crt centered banner");
        }
    }

    {
        partner_gdp emu(terminal_profile::vt100_ansi, nvram_path.string());
        emu.load_rom(rom_path.string());
        emu.reset();

        if (!wait_for_gdp_banner(emu, GDP_BANNER_ROW, GDP_BANNER_COL, banner.c_str())) {
            const auto cpu = emu.capture_debug_cpu_state();
            std::printf("FAIL gdp banner did not appear (pc=%04x sp=%04x halted=%d)\n",
                        (unsigned)cpu.pc, (unsigned)cpu.sp, cpu.halted ? 1 : 0);
            ++fails;
        } else {
            const auto &avdc = emu.get_avdc();
            if (avdc.chars_per_row != GDP_EXPECTED_COLS) {
                std::printf("FAIL gdp cols: expected %d got %u\n",
                            GDP_EXPECTED_COLS, (unsigned)avdc.chars_per_row);
                ++fails;
            }
            if (emu.get_gdp_pio_port_b() != GDP_EXPECTED_TEXT_CTL) {
                std::printf("FAIL gdp text ctl: expected %02x got %02x\n",
                            (unsigned)GDP_EXPECTED_TEXT_CTL,
                            (unsigned)emu.get_gdp_pio_port_b());
                ++fails;
            }
            if ((avdc.start1_addr & 0x3FFFu) != GDP_TEXT_BASE) {
                std::printf("FAIL gdp start1: expected %04x got %04x\n",
                            (unsigned)GDP_TEXT_BASE,
                            (unsigned)(avdc.start1_addr & 0x3FFFu));
                ++fails;
            }
            if (!expect_gdp_text(avdc,
                                 GDP_BANNER_ROW,
                                 GDP_BANNER_COL,
                                 banner.c_str(),
                                 "gdp centered banner"))
            {
                const auto model = emu.read_debug_memory(0xFD11, 1);
                const int found_row12 = find_gdp_banner_col(avdc, GDP_BANNER_ROW - 1, banner.c_str());
                const int found_row13 = find_gdp_banner_col(avdc, GDP_BANNER_ROW, banner.c_str());
                const int found_row14 = find_gdp_banner_col(avdc, GDP_BANNER_ROW + 1, banner.c_str());
                int found_any_row = -1;
                int found_any_col = -1;
                find_gdp_banner_anywhere(avdc, banner.c_str(), found_any_row, found_any_col);
                std::printf(
                    "INFO gdp model=%02x row12=%d row13=%d row14=%d any_row=%d any_col=%d "
                    "display=%d rowtbl=%d cur=%04x start1=%04x dptr=%04x char_wr=%llu nonspace=%llu terminal_rows=%zu\n",
                            model.empty() ? 0u : (unsigned)model[0],
                            found_row12,
                            found_row13,
                            found_row14,
                            found_any_row,
                            found_any_col,
                            avdc.display_enabled ? 1 : 0,
                            avdc.use_row_table ? 1 : 0,
                            (unsigned)avdc.cursor_addr,
                            (unsigned)avdc.start1_addr,
                            (unsigned)avdc.display_ptr_addr,
                            (unsigned long long)emu.get_avdc_char_writes(),
                            (unsigned long long)emu.get_avdc_char_nonspace_writes(),
                            split_lines(emu.dump_terminal_text()).size());
                ++fails;
            } else {
                fails += !expect_gdp_attr(avdc,
                                          GDP_BANNER_ROW,
                                          GDP_BANNER_COL,
                                          banner.c_str(),
                                          0x00,
                                          "gdp banner attrs");
                if (avdc.cursor_enabled) {
                    std::puts("FAIL gdp cursor should be hidden for ROM banner");
                    ++fails;
                }
            }
        }
    }

    fs::remove(nvram_path, ec);
    fs::remove(tmp_dir, ec);

    if (fails == 0) {
        std::puts("test_partos_boot_banner: PASS");
        return 0;
    }

    std::printf("test_partos_boot_banner: %d failure(s)\n", fails);
    return 1;
}
