#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "partner_crt.hpp"
#include "partner_gdp.hpp"

namespace {

constexpr uint64_t TICK_LIMIT = 4'000'000ULL;
constexpr int GDP_ROWS = 26;
constexpr int GDP_COLS = 132;
constexpr uint16_t GDP_ROW_BASE0 = 0x0100;

static bool build_print_harness(const std::filesystem::path &source_root)
{
    const std::string cmd = "make -C " + (source_root / "tests" / "partos_print").string() + " -s";
    return std::system(cmd.c_str()) == 0;
}

template <typename Emu>
static bool wait_for_halt(Emu &emu, uint64_t flush_ticks)
{
    for (uint64_t ticks = 0; ticks < TICK_LIMIT; ++ticks) {
        emu.tick();
        if (emu.capture_debug_cpu_state().halted) {
            for (uint64_t i = 0; i < flush_ticks; ++i)
                emu.tick();
            return true;
        }
    }
    return false;
}

static void configure_avdc_text_mode(partner_gdp &emu)
{
    auto &avdc = const_cast<scn2674_t &>(emu.get_avdc());

    std::memset(avdc.vram, ' ', sizeof(avdc.vram));
    std::memset(avdc.attr_vram, 0, sizeof(avdc.attr_vram));

    avdc.use_row_table = true;
    avdc.display_enabled = true;
    avdc.cursor_enabled = true;
    avdc.rows_per_screen = GDP_ROWS;
    avdc.chars_per_row = GDP_COLS;
    avdc.scanlines_per_char_row = 12;
    avdc.start1_addr = GDP_ROW_BASE0;
    avdc.start2_addr = 0;
    avdc.start2_addr_start = 0;
    avdc.display_ptr_addr = 0;
    avdc.cursor_addr = GDP_ROW_BASE0;
    avdc.busy_ticks = 0;

    for (int row = 0; row < GDP_ROWS; ++row) {
        const uint16_t base = (uint16_t)(GDP_ROW_BASE0 + row * GDP_COLS);
        const int ptr = row * 2;
        avdc.vram[ptr] = (uint8_t)(base & 0xFFu);
        avdc.vram[ptr + 1] = (uint8_t)(base >> 8);
        avdc.attr_vram[ptr] = 0;
        avdc.attr_vram[ptr + 1] = 0;
    }
}

static uint16_t read_row_base(const scn2674_t &avdc, int row)
{
    const int ptr = row * 2;
    return (uint16_t)(avdc.vram[ptr] | ((uint16_t)avdc.vram[ptr + 1] << 8));
}

static bool expect_gdp_cell(const scn2674_t &avdc,
                            int row,
                            int col,
                            char ch,
                            uint8_t attr,
                            const char *label)
{
    const uint16_t addr = (uint16_t)(read_row_base(avdc, row) + col);
    const uint8_t got_ch = avdc.vram[addr & 0x3FFFu];
    const uint8_t got_attr = avdc.attr_vram[addr & 0x3FFFu];
    if (got_ch != (uint8_t)ch || got_attr != attr) {
        std::printf("FAIL %s: row=%d col=%d expected ch=%02x attr=%02x got ch=%02x attr=%02x\n",
                    label, row, col, (unsigned)(uint8_t)ch, (unsigned)attr,
                    (unsigned)got_ch, (unsigned)got_attr);
        return false;
    }
    return true;
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

} // namespace

int main()
{
    int fails = 0;
    const std::filesystem::path root = IDP_SOURCE_ROOT;
    const std::filesystem::path rom_path = root / "tests" / "partos_print" / "build" / "print_harness.rom";

    if (!build_print_harness(root)) {
        std::puts("FAIL print harness build failed");
        return 1;
    }

    {
        partner_gdp emu(terminal_profile::vt100_ansi);
        emu.load_rom(rom_path.string());
        emu.reset();
        configure_avdc_text_mode(emu);

        if (!wait_for_halt(emu, 2048)) {
            std::puts("FAIL gdp run did not halt");
            ++fails;
        } else {
            const auto &avdc = emu.get_avdc();
            fails += !expect_gdp_cell(avdc, 0, 1, 'H', 0x10, "gdp highlight");
            fails += !expect_gdp_cell(avdc, 2, 3, 'H', 0x20, "gdp inverse H");
            fails += !expect_gdp_cell(avdc, 2, 4, 'E', 0x20, "gdp inverse E");
            fails += !expect_gdp_cell(avdc, 2, 5, 'L', 0x20, "gdp inverse L1");
            fails += !expect_gdp_cell(avdc, 2, 6, 'L', 0x20, "gdp inverse L2");
            fails += !expect_gdp_cell(avdc, 2, 7, 'O', 0x20, "gdp inverse O");
            fails += !expect_gdp_cell(avdc, 4, 0, 'N', 0x00, "gdp normal");

            const uint16_t expect_cursor = (uint16_t)(read_row_base(avdc, 4) + 1);
            if ((avdc.cursor_addr & 0x3FFFu) != expect_cursor) {
                std::printf("FAIL gdp cursor: expected %04x got %04x\n",
                            (unsigned)expect_cursor, (unsigned)(avdc.cursor_addr & 0x3FFFu));
                ++fails;
            }
        }
    }

    {
        partner_crt emu(terminal_profile::vt52);
        emu.load_rom(rom_path.string());
        emu.reset();

        if (!wait_for_halt(emu, 4096)) {
            std::puts("FAIL crt run did not halt");
            ++fails;
        } else {
            const auto lines = split_lines(emu.dump_terminal_text());
            fails += !expect_line_fragment(lines, 0, 1, "H", "crt highlight");
            fails += !expect_line_fragment(lines, 2, 3, "HELLO", "crt inverse");
            fails += !expect_line_fragment(lines, 4, 0, "N", "crt normal");
        }
    }

    if (fails == 0) {
        std::puts("test_partos_print: PASS");
        return 0;
    }

    std::printf("test_partos_print: %d failure(s)\n", fails);
    return 1;
}
