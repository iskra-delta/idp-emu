#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <regex>
#include <string>
#include <vector>

#include "partner.hpp"

namespace {

constexpr uint16_t DEV_NEXT = 0;
constexpr uint16_t DEV_NAME = 2;
constexpr uint16_t DEV_DRIVER = 28;
constexpr uint16_t DEV_SIZE = 30;
constexpr uint16_t DRV_OPEN = 4;
constexpr uint16_t DRV_CLOSE = 6;
constexpr uint16_t DRV_READ = 8;
constexpr uint16_t DRV_WRITE = 10;
constexpr uint16_t DEV_MAX = 16;

constexpr uint16_t CALL_RETURN_PC = 0xC000;
constexpr uint16_t CALL_BUFFER = 0xC100;
constexpr uint16_t CALL_STACK = 0xF400;
constexpr uint64_t BOOT_TICK_LIMIT = 2'000'000ULL;
constexpr uint64_t CALL_TICK_LIMIT = 200'000ULL;
constexpr uint8_t RTC_TIME_SIZE = 6;
constexpr uint8_t RTC_NVRAM_SIZE = 8;

static uint8_t bcd_to_int(uint8_t bcd)
{
    return (uint8_t)((((bcd >> 4) & 0x0F) * 10) + (bcd & 0x0F));
}

static uint8_t int_to_bcd(uint8_t value)
{
    return (uint8_t)(((value / 10) << 4) | (value % 10));
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
        if (std::regex_search(line, m, re))
            return (uint16_t)std::stoul(m[1].str(), nullptr, 16);
    }
    return std::nullopt;
}

static bool wait_for_halt(partner &emu, uint64_t limit)
{
    for (uint64_t ticks = 0; ticks < limit; ++ticks) {
        if (emu.capture_debug_cpu_state().halted)
            return true;
        emu.tick();
    }
    return emu.capture_debug_cpu_state().halted;
}

static uint16_t read_u16(partner &emu, uint16_t addr)
{
    const auto raw = emu.read_debug_memory(addr, 2);
    return (uint16_t)raw[0] | ((uint16_t)raw[1] << 8);
}

static std::optional<uint16_t> find_device(partner &emu, uint16_t dev_list_addr, const std::string &name)
{
    uint16_t dev = read_u16(emu, dev_list_addr);
    for (size_t i = 0; (i < DEV_MAX) && (dev != 0); ++i) {
        const auto raw = emu.read_debug_memory(dev, DEV_SIZE);
        std::string dev_name;
        for (size_t j = 0; j < 8; ++j) {
            if (raw[DEV_NAME + j] == 0)
                break;
            dev_name.push_back((char)raw[DEV_NAME + j]);
        }
        if (dev_name == name)
            return dev;
        dev = (uint16_t)raw[DEV_NEXT] | ((uint16_t)raw[DEV_NEXT + 1] << 8);
    }
    return std::nullopt;
}

struct call_result {
    bool halted = false;
    partner::debug_cpu_state cpu{};
};

static call_result call_driver(partner &emu,
                               uint16_t fn_addr,
                               uint16_t hl,
                               uint16_t de = 0,
                               uint16_t bc = 0)
{
    emu.write_debug_memory(CALL_RETURN_PC, { 0x76 });
    emu.write_debug_memory(CALL_STACK - 2, {
        (uint8_t)(CALL_RETURN_PC & 0xFF),
        (uint8_t)(CALL_RETURN_PC >> 8)
    });

    auto cpu = emu.capture_debug_cpu_state();
    cpu.af = 0;
    cpu.bc = bc;
    cpu.de = de;
    cpu.hl = hl;
    cpu.sp = CALL_STACK - 2;
    cpu.iff1 = false;
    cpu.iff2 = false;
    cpu.halted = false;
    emu.apply_debug_cpu_state(cpu);
    emu.debug_set_pc(fn_addr);

    call_result result;
    result.halted = wait_for_halt(emu, CALL_TICK_LIMIT);
    result.cpu = emu.capture_debug_cpu_state();
    return result;
}

static int expect_ok(const call_result &result, const char *label)
{
    if (!result.halted) {
        std::printf("FAIL %s: subroutine did not halt on return\n", label);
        return 1;
    }
    if (result.cpu.hl != 0x0000) {
        std::printf("FAIL %s: expected HL=0000, got %04x\n", label, result.cpu.hl);
        return 1;
    }
    return 0;
}

} // namespace

int main()
{
    int fails = 0;
    const std::filesystem::path root = IDP_SOURCE_ROOT;
    const std::filesystem::path partos_dir = root / "partos";
    const std::filesystem::path rom_path = std::filesystem::absolute(partos_dir / "bin" / "partos.rom");
    const std::filesystem::path map_path = std::filesystem::absolute(partos_dir / "build" / "partos.map");
    const std::filesystem::path tmp_dir = root / "tests" / ".tmp-partos-rtc";

    std::filesystem::create_directories(tmp_dir);
    const auto old_cwd = std::filesystem::current_path();
    std::filesystem::current_path(tmp_dir);

    if (!build_partos_rom(root)) {
        std::puts("FAIL partos build failed");
        std::filesystem::current_path(old_cwd);
        return 1;
    }

    const auto dev_list_addr = map_symbol_address(map_path, "dev_list");
    if (!dev_list_addr.has_value()) {
        std::puts("FAIL could not locate dev_list in partos.map");
        std::filesystem::current_path(old_cwd);
        return 1;
    }

    partner emu;
    emu.load_rom(rom_path.string());
    emu.reset();
    if (!wait_for_halt(emu, BOOT_TICK_LIMIT)) {
        std::puts("FAIL boot did not reach HALT");
        std::filesystem::current_path(old_cwd);
        return 1;
    }

    const auto rtc_dev = find_device(emu, *dev_list_addr, "rtc");
    const auto nvram_dev = find_device(emu, *dev_list_addr, "nvram");
    if (!rtc_dev.has_value() || !nvram_dev.has_value()) {
        std::puts("FAIL rtc/nvram devices not found");
        std::filesystem::current_path(old_cwd);
        return 1;
    }

    const uint16_t rtc_drv = read_u16(emu, (uint16_t)(*rtc_dev + DEV_DRIVER));
    const uint16_t rtc_open = read_u16(emu, (uint16_t)(rtc_drv + DRV_OPEN));
    const uint16_t rtc_close = read_u16(emu, (uint16_t)(rtc_drv + DRV_CLOSE));
    const uint16_t rtc_read = read_u16(emu, (uint16_t)(rtc_drv + DRV_READ));
    const uint16_t rtc_write = read_u16(emu, (uint16_t)(rtc_drv + DRV_WRITE));

    const uint16_t nvram_drv = read_u16(emu, (uint16_t)(*nvram_dev + DEV_DRIVER));
    const uint16_t nvram_open = read_u16(emu, (uint16_t)(nvram_drv + DRV_OPEN));
    const uint16_t nvram_close = read_u16(emu, (uint16_t)(nvram_drv + DRV_CLOSE));
    const uint16_t nvram_read = read_u16(emu, (uint16_t)(nvram_drv + DRV_READ));
    const uint16_t nvram_write = read_u16(emu, (uint16_t)(nvram_drv + DRV_WRITE));

    fails += expect_ok(call_driver(emu, rtc_open, *rtc_dev), "rtc_open");
    fails += expect_ok(call_driver(emu, rtc_read, *rtc_dev, CALL_BUFFER, RTC_TIME_SIZE), "rtc_read_default");
    const auto rtc_default = emu.read_debug_memory(CALL_BUFFER, RTC_TIME_SIZE);
    const std::array<uint8_t, RTC_TIME_SIZE> rtc_default_expected = {
        bcd_to_int(emu.get_rtc().regs[0x02]),
        bcd_to_int(emu.get_rtc().regs[0x03]),
        bcd_to_int(emu.get_rtc().regs[0x04]),
        bcd_to_int(emu.get_rtc().regs[0x06]),
        bcd_to_int(emu.get_rtc().regs[0x07]),
        bcd_to_int(emu.get_rtc().regs[0x09]),
    };
    for (size_t i = 0; i < RTC_TIME_SIZE; ++i) {
        if (rtc_default[i] != rtc_default_expected[i]) {
            std::printf("FAIL rtc_read_default: byte %zu expected %02x got %02x\n",
                        i,
                        rtc_default_expected[i],
                        rtc_default[i]);
            ++fails;
            break;
        }
    }

    fails += expect_ok(call_driver(emu, nvram_open, *nvram_dev), "nvram_open");
    fails += expect_ok(call_driver(emu, nvram_read, *nvram_dev, CALL_BUFFER, RTC_NVRAM_SIZE), "nvram_read_default");
    const auto nvram_default = emu.read_debug_memory(CALL_BUFFER, RTC_NVRAM_SIZE);
    for (size_t i = 0; i < RTC_NVRAM_SIZE; ++i) {
        if (nvram_default[i] != emu.get_rtc().regs[0x08 + i]) {
            std::printf("FAIL nvram_read_default: byte %zu expected %02x got %02x\n",
                        i,
                        emu.get_rtc().regs[0x08 + i],
                        nvram_default[i]);
            ++fails;
            break;
        }
    }

    const std::vector<uint8_t> rtc_pattern = { 12, 34, 9, 21, 11, 26 };
    emu.write_debug_memory(CALL_BUFFER, rtc_pattern);
    fails += expect_ok(call_driver(emu, rtc_open, *rtc_dev), "rtc_reopen_for_write");
    fails += expect_ok(call_driver(emu, rtc_write, *rtc_dev, CALL_BUFFER, RTC_TIME_SIZE), "rtc_write");
    fails += expect_ok(call_driver(emu, rtc_open, *rtc_dev), "rtc_reopen_for_verify");
    fails += expect_ok(call_driver(emu, rtc_read, *rtc_dev, CALL_BUFFER, RTC_TIME_SIZE), "rtc_read_back");
    const auto rtc_back = emu.read_debug_memory(CALL_BUFFER, RTC_TIME_SIZE);
    const std::array<uint8_t, RTC_TIME_SIZE> rtc_regs_back = {
        emu.get_rtc().regs[0x02],
        emu.get_rtc().regs[0x03],
        emu.get_rtc().regs[0x04],
        emu.get_rtc().regs[0x06],
        emu.get_rtc().regs[0x07],
        emu.get_rtc().regs[0x09],
    };
    for (size_t i = 0; i < RTC_TIME_SIZE; ++i) {
        if (rtc_back[i] != rtc_pattern[i] || rtc_regs_back[i] != int_to_bcd(rtc_pattern[i])) {
            std::printf("FAIL rtc_write_back: byte %zu expected %02x got drv=%02x hw=%02x\n",
                        i,
                        rtc_pattern[i],
                        rtc_back[i],
                        rtc_regs_back[i]);
            ++fails;
            break;
        }
    }

    const std::vector<uint8_t> nvram_pattern = { 0x11, 0x22, 0x33, 0x44, 0x51, 0x66, 0x77, 0x88 };
    emu.write_debug_memory(CALL_BUFFER, nvram_pattern);
    fails += expect_ok(call_driver(emu, nvram_open, *nvram_dev), "nvram_reopen_for_write");
    fails += expect_ok(call_driver(emu, nvram_write, *nvram_dev, CALL_BUFFER, RTC_NVRAM_SIZE), "nvram_write");
    fails += expect_ok(call_driver(emu, nvram_open, *nvram_dev), "nvram_reopen_for_verify");
    fails += expect_ok(call_driver(emu, nvram_read, *nvram_dev, CALL_BUFFER, RTC_NVRAM_SIZE), "nvram_read_back");
    const auto nvram_back = emu.read_debug_memory(CALL_BUFFER, RTC_NVRAM_SIZE);
    for (size_t i = 0; i < RTC_NVRAM_SIZE; ++i) {
        if (nvram_back[i] != nvram_pattern[i] || emu.get_rtc().regs[0x08 + i] != nvram_pattern[i]) {
            std::printf("FAIL nvram_write_back: byte %zu expected %02x got drv=%02x hw=%02x\n",
                        i,
                        nvram_pattern[i],
                        nvram_back[i],
                        emu.get_rtc().regs[0x08 + i]);
            ++fails;
            break;
        }
    }

    emu.write_debug_memory(CALL_BUFFER, rtc_default);
    fails += expect_ok(call_driver(emu, rtc_open, *rtc_dev), "rtc_reopen_for_restore");
    fails += expect_ok(call_driver(emu, rtc_write, *rtc_dev, CALL_BUFFER, RTC_TIME_SIZE), "rtc_restore");
    emu.write_debug_memory(CALL_BUFFER, nvram_default);
    fails += expect_ok(call_driver(emu, nvram_open, *nvram_dev), "nvram_reopen_for_restore");
    fails += expect_ok(call_driver(emu, nvram_write, *nvram_dev, CALL_BUFFER, RTC_NVRAM_SIZE), "nvram_restore");

    call_driver(emu, rtc_close, *rtc_dev);
    call_driver(emu, nvram_close, *nvram_dev);
    std::filesystem::current_path(old_cwd);

    if (fails == 0) {
        std::puts("test_partos_bios_rtc: PASS");
        return 0;
    }

    std::printf("test_partos_bios_rtc: %d failure(s)\n", fails);
    return 1;
}
