#include "partner_gdp.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace {

constexpr uint64_t BOOT_TICK_LIMIT = 80'000'000ULL;
constexpr uint64_t COMMAND_TICK_LIMIT = 20'000'000ULL;
constexpr uint64_t KEY_TICKS = 5'000ULL;

bool build_all(const std::filesystem::path &root)
{
    const std::string build_cmd =
        "make -C " + (root / "partos").string() + " -s sys rom";
    if (std::system(build_cmd.c_str()) != 0)
        return false;

    const std::string disk_cmd =
        "python3 " + (root / "tools" / "mkdosdisk.py").string() + " " +
        (root / "disks").string();
    return std::system(disk_cmd.c_str()) == 0;
}

bool copy_file_binary(const std::filesystem::path &src,
                      const std::filesystem::path &dst)
{
    std::ifstream in(src, std::ios::binary);
    std::ofstream out(dst, std::ios::binary | std::ios::trunc);

    out << in.rdbuf();
    return (bool)in && (bool)out;
}

std::vector<std::string> visible_rows(const scn2674_t &avdc)
{
    const int rows = 26;
    const int cols = std::min(132, std::max(1, (int)avdc.chars_per_row));
    std::vector<std::string> out(rows);

    for (int row = 0; row < rows; ++row) {
        const uint16_t p = (uint16_t)((row * 2) & 0x3FFFu);
        const uint16_t raw =
            (uint16_t)(avdc.vram[p] | ((uint16_t)avdc.vram[(p + 1) & 0x3FFFu] << 8));
        const uint16_t base = (uint16_t)(raw & 0x3FFFu);

        std::string line;
        line.reserve((size_t)cols);
        for (int col = 0; col < cols; ++col) {
            const uint8_t ch = avdc.vram[(base + col) & 0x3FFFu];
            line.push_back((ch >= 0x20 && ch < 0x7F) ? (char)ch : '.');
        }
        while (!line.empty() && line.back() == ' ')
            line.pop_back();
        out[(size_t)row] = line;
    }

    return out;
}

bool is_prompt_row(const std::string &row)
{
    return (row.size() >= 4u) &&
           ((row[0] == 'h') || (row[0] == 'f')) &&
           (row[1] == 'd') &&
           (row[2] >= '0') && (row[2] <= '9') &&
           (row.find('>') != std::string::npos);
}

int find_last_prompt_row(const std::vector<std::string> &rows)
{
    int best = -1;

    for (int row = 0; row < (int)rows.size(); ++row) {
        if (is_prompt_row(rows[(size_t)row]))
            best = row;
    }

    return best;
}

bool rows_contain(const std::vector<std::string> &rows, const std::string &needle)
{
    for (const std::string &row : rows) {
        if (row.find(needle) != std::string::npos)
            return true;
    }

    return false;
}

void dump_rows(const std::vector<std::string> &rows)
{
    for (int row = 0; row < (int)rows.size(); ++row)
        std::printf("row%02d \"%s\"\n", row, rows[(size_t)row].c_str());
}

void send_text(partner_gdp &emu, const std::string &text)
{
    for (char ch : text) {
        emu.key_input((uint8_t)ch);
        for (uint64_t i = 0; i < KEY_TICKS; ++i)
            emu.tick();
    }
}

bool wait_for_rows_with_prompt(partner_gdp &emu,
                               uint64_t tick_limit,
                               std::vector<std::string> &rows_out)
{
    for (uint64_t i = 0; i < tick_limit; ++i) {
        emu.tick();
        if ((i & 0x1FFFu) != 0)
            continue;

        rows_out = visible_rows(emu.get_avdc());
        if (find_last_prompt_row(rows_out) >= 0)
            return true;
    }

    rows_out = visible_rows(emu.get_avdc());
    return false;
}

bool wait_for_next_prompt(partner_gdp &emu,
                          int prev_prompt_row,
                          std::vector<std::string> &rows_out)
{
    for (uint64_t i = 0; i < COMMAND_TICK_LIMIT; ++i) {
        emu.tick();
        if ((i & 0x1FFFu) != 0)
            continue;

        rows_out = visible_rows(emu.get_avdc());
        if (find_last_prompt_row(rows_out) > prev_prompt_row)
            return true;
    }

    rows_out = visible_rows(emu.get_avdc());
    return false;
}

} // namespace

int main()
{
    namespace fs = std::filesystem;

    const fs::path root = IDP_SOURCE_ROOT;
    const fs::path tmp_dir = root / "tests" / ".tmp-partos-cp-return";
    const fs::path nvram_path = tmp_dir / "shadow-nvram.bin";
    const fs::path hdd_path = tmp_dir / "hdd-dos.img";
    const fs::path src_nvram = root / "partos" / "partos_shadow_nvram.bin";
    const fs::path src_hdd = root / "disks" / "hdd-dos.img";
    const std::string copy_name = "CPRET1.COM";
    std::error_code ec;
    std::vector<std::string> rows;

    auto cleanup = [&]() {
        fs::remove(hdd_path, ec);
        fs::remove(nvram_path, ec);
        fs::remove(tmp_dir, ec);
    };

    if (!build_all(root)) {
        std::puts("FAIL build_all");
        return 1;
    }

    fs::create_directories(tmp_dir, ec);
    if (!copy_file_binary(src_nvram, nvram_path) ||
        !copy_file_binary(src_hdd, hdd_path)) {
        std::puts("FAIL copy test fixtures");
        cleanup();
        return 1;
    }

    partner_gdp emu(terminal_profile::vt100_ansi, nvram_path.string());
    emu.load_rom((root / "partos" / "bin" / "partos.rom").string());
    emu.load_disk(0, (root / "disks" / "fdd-dos.img").string());
    emu.load_hdd(hdd_path.string());
    emu.reset();

    if (!wait_for_rows_with_prompt(emu, BOOT_TICK_LIMIT, rows)) {
        std::puts("FAIL no initial shell prompt");
        dump_rows(rows);
        cleanup();
        return 1;
    }

    int prompt_row = find_last_prompt_row(rows);
    send_text(emu, "cp /ls.com /" + copy_name + "\r");
    if (!wait_for_next_prompt(emu, prompt_row, rows)) {
        std::puts("FAIL cp did not return to shell");
        dump_rows(rows);
        cleanup();
        return 1;
    }
    if (rows_contain(rows, "?")) {
        std::puts("FAIL cp reported an error");
        dump_rows(rows);
        cleanup();
        return 1;
    }

    prompt_row = find_last_prompt_row(rows);
    send_text(emu, "ls\r");
    if (!wait_for_next_prompt(emu, prompt_row, rows)) {
        std::puts("FAIL ls after cp did not return to shell");
        dump_rows(rows);
        cleanup();
        return 1;
    }
    if (!rows_contain(rows, "cpret1.com")) {
        std::puts("FAIL copied file did not appear in directory listing");
        dump_rows(rows);
        cleanup();
        return 1;
    }

    cleanup();
    std::puts("test_partos_cp_return: PASS");
    return 0;
}
