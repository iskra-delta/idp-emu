#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <regex>
#include <string>
#include <vector>

#include "partner_gdp.hpp"

namespace {

constexpr uint16_t DEV_NEXT = 0;
constexpr uint16_t DEV_NAME = 2;
constexpr uint16_t DEV_SIZE = 30;
constexpr size_t DEV_MAX = 16;
constexpr uint64_t PARTOS_BOOT_TICK_LIMIT = 2'000'000ULL;
constexpr uint64_t FDD_IMAGE_SIZE = 80ULL * 2 * 18 * 256;
constexpr uint64_t HDD_IMAGE_SIZE = 1224ULL * 32 * 256;

struct scenario {
    bool fd0 = false;
    bool fd1 = false;
    bool hdd = false;
    std::vector<std::string> expected_names;
};

static std::string join_names(const std::vector<std::string> &names)
{
    std::string out;
    for (size_t i = 0; i < names.size(); ++i) {
        if (i != 0)
            out += ",";
        out += names[i];
    }
    return out;
}

static bool create_sparse_image(const std::filesystem::path &path, uint64_t size)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file)
        return false;
    if (size == 0)
        return true;
    file.seekp((std::streamoff)(size - 1));
    file.put('\0');
    return (bool)file;
}

static bool build_partos_rom(const std::filesystem::path &source_root)
{
    const std::string cmd = "make -C " + (source_root / "partos").string() + " -s";
    return std::system(cmd.c_str()) == 0;
}

static std::optional<uint16_t> map_symbol_address(const std::filesystem::path &map_path,
                                                  const std::string &symbol)
{
    std::ifstream file(map_path);
    if (!file)
        return std::nullopt;

    const std::regex re("([0-9A-Fa-f]{4,8})\\s+" + symbol + "\\b");
    std::string line;
    while (std::getline(file, line)) {
        std::smatch m;
        if (std::regex_search(line, m, re)) {
            return (uint16_t)std::stoul(m[1].str(), nullptr, 16);
        }
    }
    return std::nullopt;
}

static bool wait_for_boot_halt(partner_gdp &emu)
{
    for (uint64_t ticks = 0; ticks < PARTOS_BOOT_TICK_LIMIT; ++ticks) {
        if (emu.capture_debug_cpu_state().halted)
            return true;
        emu.tick();
    }
    return emu.capture_debug_cpu_state().halted;
}

static std::vector<std::string> read_device_names(partner_gdp &emu, uint16_t dev_list_addr)
{
    std::vector<std::string> names;
    const auto head_bytes = emu.read_debug_memory(dev_list_addr, 2);
    uint16_t dev = (uint16_t)head_bytes[0] | ((uint16_t)head_bytes[1] << 8);

    for (size_t i = 0; (i < DEV_MAX) && (dev != 0); ++i) {
        const auto raw = emu.read_debug_memory(dev, DEV_SIZE);
        std::string name;
        for (size_t j = 0; j < 8; ++j) {
            const uint8_t ch = raw[DEV_NAME + j];
            if (ch == 0)
                break;
            name.push_back((char)ch);
        }
        names.push_back(name);
        dev = (uint16_t)raw[DEV_NEXT] | ((uint16_t)raw[DEV_NEXT + 1] << 8);
    }
    return names;
}

static int expect_probe_result(const std::filesystem::path &rom_path,
                               uint16_t dev_list_addr,
                               const std::filesystem::path &fd0_path,
                               const std::filesystem::path &fd1_path,
                               const std::filesystem::path &hdd_path,
                               const scenario &cfg,
                               const char *label)
{
    int fails = 0;
    partner_gdp emu(terminal_profile::vt100_ansi);
    emu.load_rom(rom_path.string());
    if (cfg.fd0)
        emu.load_disk(0, fd0_path.string());
    if (cfg.fd1)
        emu.load_disk(1, fd1_path.string());
    if (cfg.hdd)
        emu.load_hdd(hdd_path.string());
    emu.reset();

    if (!wait_for_boot_halt(emu)) {
        std::printf("FAIL %s: boot did not reach HALT after probe\n", label);
        return 1;
    }

    const auto names = read_device_names(emu, dev_list_addr);
    if (names != cfg.expected_names) {
        std::printf("FAIL %s: expected [%s], got [%s]\n",
                    label,
                    join_names(cfg.expected_names).c_str(),
                    join_names(names).c_str());
        ++fails;
    }
    return fails;
}

} // namespace

int main()
{
    int fails = 0;
    const std::filesystem::path root = IDP_SOURCE_ROOT;
    const std::filesystem::path partos_dir = root / "partos";
    const std::filesystem::path rom_path = partos_dir / "bin" / "partos.rom";
    const std::filesystem::path map_path = partos_dir / "build" / "partos.map";
    const std::filesystem::path tmp_dir = root / "tests" / ".tmp-partos-probe-gdp";
    const std::filesystem::path fd0_path = tmp_dir / "fd0.img";
    const std::filesystem::path fd1_path = tmp_dir / "fd1.img";
    const std::filesystem::path hdd_path = tmp_dir / "hdd.img";

    if (!build_partos_rom(root)) {
        std::puts("FAIL partos build failed");
        return 1;
    }

    const auto dev_list_addr = map_symbol_address(map_path, "dev_list");
    if (!dev_list_addr.has_value()) {
        std::puts("FAIL could not locate dev_list in partos.map");
        return 1;
    }

    std::filesystem::create_directories(tmp_dir);
    if (!create_sparse_image(fd0_path, FDD_IMAGE_SIZE) ||
        !create_sparse_image(fd1_path, FDD_IMAGE_SIZE) ||
        !create_sparse_image(hdd_path, HDD_IMAGE_SIZE))
    {
        std::puts("FAIL could not create synthetic disk images");
        return 1;
    }

    fails += expect_probe_result(
        rom_path, *dev_list_addr, fd0_path, fd1_path, hdd_path,
        scenario{ false, false, false, { "ttyS0", "ttyS1", "ttyS2", "ttyS3", "rtc", "nvram", "gdp" } }, "gdp_no_media");

    fails += expect_probe_result(
        rom_path, *dev_list_addr, fd0_path, fd1_path, hdd_path,
        scenario{ true, true, true, { "ttyS0", "ttyS1", "ttyS2", "ttyS3", "rtc", "nvram", "gdp", "fd0", "fd1", "sda" } }, "gdp_combined");

    if (fails == 0) {
        std::puts("test_partos_bios_probe_gdp: PASS");
        return 0;
    }

    std::printf("test_partos_bios_probe_gdp: %d failure(s)\n", fails);
    return 1;
}
