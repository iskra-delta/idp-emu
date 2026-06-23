// probe_kernel.cpp
//
// Direct split-image boot probe for PartOS.
//
// This bypasses the ROM loader completely and verifies the next stage of the
// boot contract in isolation:
//
//   1. disable the ROM overlay,
//   2. jump to the micro-kernel entry at 0x0000,
//   3. let the kernel initialize and start the first payload thread,
//   4. confirm that control reaches the fixed OS payload entry at 0xC000 and
//      then the higher-level bootstrap thread.
//
// It is intentionally narrower than a full OS boot. The goal is to prove the
// kernel -> OS handoff works with the split `kernel.sys` / `os.sys` images.
//
// 2026-06-22   tstih

#include "partner.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <regex>
#include <string>
#include <vector>

namespace {

constexpr uint16_t STUB_ADDR      = 0x2000;
constexpr uint16_t KERNEL_ENTRY   = 0x0000;
constexpr uint64_t RUN_TICK_LIMIT = 4'000'000ULL;
constexpr uint16_t THREAD_SP_OFF = 4;
constexpr uint16_t THREAD_STARTUP_OFF = 6;
constexpr uint16_t THREAD_BANK_OFF = 24;
constexpr uint16_t CONTEXT_SIZE = 22;

struct symbol_map {
    std::map<std::string, std::vector<uint16_t>> syms;
    explicit symbol_map(const std::string &path) {
        std::ifstream f(path);
        std::string line;
        const std::regex re("([0-9A-Fa-f]{8})  ([_A-Za-z.][_A-Za-z0-9$.]*)");
        while (std::getline(f, line)) {
            for (auto it = std::sregex_iterator(line.begin(), line.end(), re);
                 it != std::sregex_iterator(); ++it) {
                const uint16_t a =
                    (uint16_t)std::stoul((*it)[1].str(), nullptr, 16);
                syms[(*it)[2].str()].push_back(a);
            }
        }
        for (auto &kv : syms) {
            std::sort(kv.second.begin(), kv.second.end());
            kv.second.erase(std::unique(kv.second.begin(), kv.second.end()),
                            kv.second.end());
        }
    }
    uint16_t at(const std::string &name, size_t idx = 0) const {
        auto it = syms.find(name);
        if (it == syms.end())
            it = syms.find(name.substr(0, 9));
        if (it == syms.end() || idx >= it->second.size()) {
            std::printf("FATAL: symbol '%s'[%zu] not in map\n",
                        name.c_str(), idx);
            std::exit(2);
        }
        return it->second[idx];
    }
};

static std::vector<uint8_t> read_file(const std::string &path)
{
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f)
        return {};
    const auto n = (size_t)f.tellg();
    std::vector<uint8_t> data(n);
    f.seekg(0);
    f.read(reinterpret_cast<char *>(data.data()), (std::streamsize)n);
    return data;
}

static uint16_t read_area_addr(const std::string &path, const std::string &area)
{
    std::ifstream f(path);
    std::string line;
    const std::regex re("^" + area + R"(\s+([0-9A-Fa-f]{8})\s+)");
    std::smatch m;
    while (std::getline(f, line)) {
        if (std::regex_search(line, m, re))
            return (uint16_t)std::stoul(m[1].str(), nullptr, 16);
    }
    std::printf("FATAL: area %s not in %s\n", area.c_str(), path.c_str());
    std::exit(2);
}

static bool build_all(const std::string &root)
{
    if (std::system(("make -C " + root + "/partos -s sys").c_str()) != 0) {
        std::puts("FAIL: split PartOS build failed");
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char **argv)
{
    std::string root = ".";
#ifdef IDP_SOURCE_ROOT
    root = IDP_SOURCE_ROOT;
#endif
    if (argc > 1)
        root = argv[1];

    if (std::getenv("IDP_SKIP_BUILD") == nullptr && !build_all(root))
        return 1;

    const std::string kernel_path = root + "/partos/bin/kernel.sys";
    const std::string os_path = root + "/partos/bin/os.sys";
    const std::string kernel_map_path = root + "/partos/build/kernel.map";
    const std::string os_map_path = root + "/partos/build/os.map";

    const auto kernel_img = read_file(kernel_path);
    const auto os_img = read_file(os_path);
    if (kernel_img.empty()) {
        std::printf("FAIL: cannot read %s\n", kernel_path.c_str());
        return 1;
    }
    if (os_img.empty()) {
        std::printf("FAIL: cannot read %s\n", os_path.c_str());
        return 1;
    }

    const uint16_t kernel_base = read_area_addr(kernel_map_path, "_CODE");
    const uint16_t os_base = read_area_addr(os_map_path, "_CODE");
    symbol_map K(kernel_map_path);
    symbol_map O(os_map_path);

    const uint16_t sys_kernel = K.at("__sys_kernel");
    const uint16_t os_entry = O.at("__os_entry");
    const uint16_t os_drv_register_all = O.at("__drv_register_all");
    const uint16_t os_dev_init = O.at("__dev_init");
    const uint16_t os_dev_init_all = O.at("__dev_init_all");
    const uint16_t os_dev_probe_all = O.at("__dev_probe_all");
    const uint16_t os_syscall_init = O.at("_syscall_init");
    const uint16_t ir_enable = K.at("_ir_enable");
    const uint16_t os_bootstrap = O.at("_kernel_bootstrap");
    const uint16_t thread_current = K.at("_thread_current");
    const uint16_t thread_suspended = K.at("_thread_first_suspended");
    const uint16_t thread_running = K.at("_thread_first_running");
    const uint16_t thread_waiting = K.at("_thread_first_waiting");
    const uint16_t thread_terminated = K.at("_thread_first_terminated");

    if (kernel_base != 0x0000) {
        std::printf("FAIL: kernel _CODE base is %04X, expected 0000\n", kernel_base);
        return 1;
    }
    if (os_base != 0xC000) {
        std::printf("FAIL: os _CODE base is %04X, expected C000\n", os_base);
        return 1;
    }
    if (os_entry != 0xC000) {
        std::printf("FAIL: __os_entry is %04X, expected C000\n", os_entry);
        return 1;
    }

    partner emu(root + "/partos/partos_shadow_nvram.bin");
    emu.reset();
    emu.clean_kernel_io_handoff();

    static const std::array<uint8_t, 8> k_nvram = {
        0x00, 0x40, 0x80, 0x3F, 0x00, 0x00, 0x00, 0x02,
    };
    emu.seed_cmos_nvram(k_nvram.data(), k_nvram.size());

    emu.write_debug_memory(os_base, os_img);

    const std::vector<uint8_t> stub = {
        0xD3, 0x80,                         // out (0x80),a -> disable ROM
        0x76,                               // halt        -> stop once RAM is visible
    };
    emu.write_debug_memory(STUB_ADDR, stub);

    auto st = emu.capture_debug_cpu_state();
    st.af = 0x0000;
    st.bc = 0x0000;
    st.de = 0x0000;
    st.hl = 0x0000;
    st.ix = 0x0000;
    st.iy = 0x0000;
    st.sp = 0xFFFF;
    st.halted = false;
    emu.apply_debug_cpu_state(st);
    emu.debug_set_pc(STUB_ADDR);

    for (uint64_t guard = 0; guard < 1000; ++guard) {
        emu.tick();
        if (!emu.is_rom_enabled() && emu.capture_debug_cpu_state().halted)
            break;
    }
    if (emu.is_rom_enabled()) {
        std::puts("FAIL: direct probe never disabled the ROM overlay");
        return 1;
    }

    emu.write_debug_memory(kernel_base, kernel_img);
    st = emu.capture_debug_cpu_state();
    st.halted = false;
    emu.apply_debug_cpu_state(st);
    emu.debug_set_pc(KERNEL_ENTRY);

    bool hit_page0 = false;
    bool hit_kernel = false;
    bool hit_os_entry = false;
    bool hit_drv_register_all = false;
    bool hit_dev_init = false;
    bool hit_dev_init_all = false;
    bool hit_dev_probe_all = false;
    bool hit_syscall_init = false;
    bool hit_ir_enable = false;
    bool hit_bootstrap = false;
    uint64_t tick_page0 = 0;
    uint64_t tick_kernel = 0;
    uint64_t tick_os_entry = 0;
    uint64_t tick_drv_register_all = 0;
    uint64_t tick_dev_init = 0;
    uint64_t tick_dev_init_all = 0;
    uint64_t tick_dev_probe_all = 0;
    uint64_t tick_syscall_init = 0;
    uint64_t tick_ir_enable = 0;
    uint64_t tick_bootstrap = 0;

    for (uint64_t guard = 0; guard < RUN_TICK_LIMIT; ++guard) {
        emu.tick();
        const uint16_t pc = emu.get_current_pc();
        if (!hit_page0 && pc == KERNEL_ENTRY) {
            hit_page0 = true;
            tick_page0 = emu.get_tick_count();
        }
        if (!hit_kernel && pc == sys_kernel) {
            hit_kernel = true;
            tick_kernel = emu.get_tick_count();
        }
        if (!hit_os_entry && pc == os_entry) {
            hit_os_entry = true;
            tick_os_entry = emu.get_tick_count();
        }
        if (!hit_drv_register_all && pc == os_drv_register_all) {
            hit_drv_register_all = true;
            tick_drv_register_all = emu.get_tick_count();
        }
        if (!hit_dev_init && pc == os_dev_init) {
            hit_dev_init = true;
            tick_dev_init = emu.get_tick_count();
        }
        if (!hit_dev_init_all && pc == os_dev_init_all) {
            hit_dev_init_all = true;
            tick_dev_init_all = emu.get_tick_count();
        }
        if (!hit_dev_probe_all && pc == os_dev_probe_all) {
            hit_dev_probe_all = true;
            tick_dev_probe_all = emu.get_tick_count();
        }
        if (!hit_syscall_init && pc == os_syscall_init) {
            hit_syscall_init = true;
            tick_syscall_init = emu.get_tick_count();
        }
        if (!hit_ir_enable && pc == ir_enable) {
            hit_ir_enable = true;
            tick_ir_enable = emu.get_tick_count();
        }
        if (!hit_bootstrap && pc == os_bootstrap) {
            hit_bootstrap = true;
            tick_bootstrap = emu.get_tick_count();
            break;
        }
    }

    std::printf("partos_kernel_boot milestones:\n");
    std::printf("  [%c] page0 entry      @ 0x%04X tick=%llu\n",
                hit_page0 ? 'x' : ' ', KERNEL_ENTRY,
                (unsigned long long)tick_page0);
    std::printf("  [%c] __sys_kernel     @ 0x%04X tick=%llu\n",
                hit_kernel ? 'x' : ' ', sys_kernel,
                (unsigned long long)tick_kernel);
    std::printf("  [%c] __os_entry       @ 0x%04X tick=%llu\n",
                hit_os_entry ? 'x' : ' ', os_entry,
                (unsigned long long)tick_os_entry);
    std::printf("  [%c] __drv_register_all @ 0x%04X tick=%llu\n",
                hit_drv_register_all ? 'x' : ' ', os_drv_register_all,
                (unsigned long long)tick_drv_register_all);
    std::printf("  [%c] __dev_init       @ 0x%04X tick=%llu\n",
                hit_dev_init ? 'x' : ' ', os_dev_init,
                (unsigned long long)tick_dev_init);
    std::printf("  [%c] __dev_init_all   @ 0x%04X tick=%llu\n",
                hit_dev_init_all ? 'x' : ' ', os_dev_init_all,
                (unsigned long long)tick_dev_init_all);
    std::printf("  [%c] __dev_probe_all  @ 0x%04X tick=%llu\n",
                hit_dev_probe_all ? 'x' : ' ', os_dev_probe_all,
                (unsigned long long)tick_dev_probe_all);
    std::printf("  [%c] _syscall_init    @ 0x%04X tick=%llu\n",
                hit_syscall_init ? 'x' : ' ', os_syscall_init,
                (unsigned long long)tick_syscall_init);
    std::printf("  [%c] _ir_enable       @ 0x%04X tick=%llu\n",
                hit_ir_enable ? 'x' : ' ', ir_enable,
                (unsigned long long)tick_ir_enable);
    std::printf("  [%c] _kernel_bootstrap@ 0x%04X tick=%llu\n",
                hit_bootstrap ? 'x' : ' ', os_bootstrap,
                (unsigned long long)tick_bootstrap);

    if (!hit_page0 || !hit_kernel || !hit_os_entry || !hit_bootstrap) {
        const auto cpu = emu.capture_debug_cpu_state();
        auto rd16 = [&](uint16_t addr) -> uint16_t {
            return (uint16_t)(emu.peek_mem(addr) |
                              (emu.peek_mem((uint16_t)(addr + 1)) << 8));
        };
        const uint16_t current = rd16(thread_current);
        const uint16_t suspended = rd16(thread_suspended);
        const uint16_t running = rd16(thread_running);
        const uint16_t waiting = rd16(thread_waiting);
        const uint16_t terminated = rd16(thread_terminated);
        std::printf("FAIL: direct split boot stopped at pc=%04X sp=%04X iff1=%d iff2=%d halted=%d tick=%llu\n",
                    emu.get_current_pc(), cpu.sp,
                    cpu.iff1 ? 1 : 0, cpu.iff2 ? 1 : 0, cpu.halted ? 1 : 0,
                    (unsigned long long)emu.get_tick_count());
        std::printf("  bank=%u current=%04X suspended=%04X running=%04X waiting=%04X terminated=%04X os_entry=%04X\n",
                    (unsigned)emu.get_ram_bank(),
                    current, suspended, running, waiting, terminated, os_entry);
        const std::array<uint16_t, 4> heads = { running, suspended, waiting, terminated };
        for (size_t i = 0; i < heads.size(); ++i) {
            const uint16_t t = heads[i];
            if (t == 0)
                continue;
            const uint16_t tsp = rd16((uint16_t)(t + THREAD_SP_OFF));
            const uint16_t tret = rd16((uint16_t)(tsp + CONTEXT_SIZE - 2));
            std::printf("  list[%zu]=%04X sp=%04X ret=%04X startup=%04X bank=%02X next=%04X\n",
                        i, t, tsp, tret, (uint16_t)(t + THREAD_STARTUP_OFF),
                        emu.peek_mem((uint16_t)(t + THREAD_BANK_OFF)),
                        rd16(t));
        }
        return 1;
    }

    std::printf("PASS: direct split boot reached the OS payload bootstrap in %llu ticks\n",
                (unsigned long long)emu.get_tick_count());
    return 0;
}
