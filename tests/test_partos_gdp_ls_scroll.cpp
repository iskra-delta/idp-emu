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
constexpr uint64_t COMMAND_SETTLE_TICKS = 12'000'000ULL;
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

bool copy_nvram_file(const std::filesystem::path &src, const std::filesystem::path &dst)
{
    std::ifstream in(src, std::ios::binary);
    std::ofstream out(dst, std::ios::binary | std::ios::trunc);
    out << in.rdbuf();
    return (bool)in && (bool)out;
}

std::vector<std::string> visible_rows(const scn2674_t &avdc,
                                      std::vector<uint16_t> *raw_entries = nullptr)
{
    const int rows = 26;
    const int cols = std::min(132, std::max(1, (int)avdc.chars_per_row));
    std::vector<std::string> out(rows);
    if (raw_entries)
        raw_entries->assign(rows, 0);

    for (int row = 0; row < rows; ++row) {
        const uint16_t p = (uint16_t)((row * 2) & 0x3FFFu);
        const uint16_t raw =
            (uint16_t)(avdc.vram[p] | ((uint16_t)avdc.vram[(p + 1) & 0x3FFFu] << 8));
        if (raw_entries)
            (*raw_entries)[(size_t)row] = raw;
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

void send_text(partner_gdp &emu, const std::string &text)
{
    for (char ch : text) {
        emu.key_input((uint8_t)ch);
        for (uint64_t i = 0; i < KEY_TICKS; ++i)
            emu.tick();
    }
}

void dump_rows(const std::vector<std::string> &rows)
{
    for (int row = 0; row < (int)rows.size(); ++row)
        std::printf("row%02d \"%s\"\n", row, rows[(size_t)row].c_str());
}

bool trailing_attrs_are_clean(const scn2674_t &avdc)
{
    std::vector<uint16_t> raw_entries;
    const auto rows = visible_rows(avdc, &raw_entries);
    const int cols = std::min(132, std::max(1, (int)avdc.chars_per_row));

    for (int row = 0; row < (int)rows.size(); ++row) {
        const uint16_t base = (uint16_t)(raw_entries[(size_t)row] & 0x3FFFu);
        int last_nonblank = -1;
        for (int col = 0; col < cols; ++col) {
            if (avdc.vram[(base + col) & 0x3FFFu] > 0x20u)
                last_nonblank = col;
        }
        for (int col = last_nonblank + 1; col < cols; ++col) {
            const uint8_t attr = avdc.attr_vram[(base + col) & 0x3FFFu];
            if (attr != 0u) {
                std::printf(
                    "FAIL trailing attr leak row=%d col=%d attr=%02x base=%04x raw=%04x text=\"%s\"\n",
                    row, col, (unsigned)attr, (unsigned)base,
                    (unsigned)raw_entries[(size_t)row], rows[(size_t)row].c_str());
                dump_rows(rows);
                return false;
            }
        }
    }

    return true;
}

} // namespace

int main()
{
    namespace fs = std::filesystem;
    const fs::path root = IDP_SOURCE_ROOT;
    const fs::path tmp_dir = root / "tests" / ".tmp-partos-gdp-ls-scroll";
    const fs::path nvram_path = tmp_dir / "shadow-nvram.bin";
    const fs::path src_nvram = root / "partos" / "partos_shadow_nvram.bin";

    if (!build_all(root)) {
        std::puts("FAIL build_all");
        return 1;
    }

    std::error_code ec;
    fs::create_directories(tmp_dir, ec);
    if (!copy_nvram_file(src_nvram, nvram_path)) {
        std::puts("FAIL copy nvram");
        return 1;
    }

    partner_gdp emu(terminal_profile::vt100_ansi, nvram_path.string());
    emu.load_rom((root / "partos" / "bin" / "partos.rom").string());
    emu.load_disk(0, (root / "disks" / "fdd-dos.img").string());
    emu.load_hdd((root / "disks" / "hdd-dos.img").string());
    emu.reset();

    bool saw_prompt = false;
    for (uint64_t i = 0; i < BOOT_TICK_LIMIT; ++i) {
        emu.tick();
        if ((i & 0x1FFFu) != 0)
            continue;
        const auto rows = visible_rows(emu.get_avdc());
        if (find_last_prompt_row(rows) >= 0) {
            saw_prompt = true;
            break;
        }
    }

    if (!saw_prompt) {
        std::puts("FAIL no shell prompt");
        dump_rows(visible_rows(emu.get_avdc()));
        return 1;
    }

    send_text(emu, "ls\r");
    for (uint64_t i = 0; i < COMMAND_SETTLE_TICKS; ++i)
        emu.tick();

    send_text(emu, "ls\r");
    for (uint64_t i = 0; i < COMMAND_SETTLE_TICKS; ++i)
        emu.tick();

    const bool ok = trailing_attrs_are_clean(emu.get_avdc());

    fs::remove(nvram_path, ec);
    fs::remove(tmp_dir, ec);

    if (!ok)
        return 1;

    std::puts("test_partos_gdp_ls_scroll: PASS");
    return 0;
}
