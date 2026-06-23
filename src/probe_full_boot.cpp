// probe_full_boot.cpp
//
// End-to-end split-boot probe for PartOS:
//
//   ROM stage-0 -> inflate stage-1 @ 0x2000
//   stage-1     -> disable the overlay, read sector 0 to scratch,
//                  load kernel.sys to 0x0000 and os.sys to 0xC000
//   handoff     -> jump to 0x0000
//   kernel      -> initialize, start the scheduler, and launch the payload
//   payload     -> reach __os_entry at 0xC000 and continue to _kernel_bootstrap
//
// This deliberately stops at the OS bootstrap handoff. It proves the ROM and
// kernel contracts are aligned without requiring the rest of the OS boot path
// to complete.
//
// 2026-06-22   tstih

#include "partner_crt.hpp"

#include <algorithm>
#include <array>
#include <deque>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <regex>
#include <string>
#include <vector>

namespace {

constexpr uint16_t PC_STAGE1_READY = 0x2003;
constexpr uint16_t PC_BOOT_HD      = 0x2052;
constexpr uint16_t KERNEL_ENTRY    = 0x0000;
constexpr uint16_t ADDR_MODEL      = 0xDE0E;
constexpr uint16_t ADDR_NVRAM_CACHE = 0xDE0F;
constexpr uint64_t RUN_TICK_LIMIT  = 40'000'000ULL;
constexpr uint16_t KERNEL_IM2_BASE = 0xFE00;
static bool shell_prompt_seen(const partner_crt &emu)
{
    const std::string term = emu.dump_terminal_text();
    if (term.find("PARTOS shell") != std::string::npos &&
        term.find("> ") != std::string::npos)
        return true;
    const std::string raw = emu.dump_raw_serial_text();
    return raw.find("PARTOS shell") != std::string::npos &&
           raw.find("> ") != std::string::npos;
}

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

struct page0_reentry {
    uint16_t from_pc = 0;
    uint16_t from_sp = 0;
    uint8_t i = 0;
    bool iff1 = false;
    bool iff2 = false;
    bool halted = false;
};

struct sched_snapshot {
    uint64_t tick = 0;
    uint16_t pc = 0;
    uint16_t current = 0;
    uint16_t running = 0;
    uint16_t waiting = 0;
    uint8_t boot_evt_state = 0xFF;
    uint8_t io_evt_state = 0xFF;
};

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
    if (std::system(("make -C " + root + "/partos -s sys rom").c_str()) != 0) {
        std::puts("FAIL: PartOS ROM/sys build failed");
        return false;
    }
    if (std::system(("python3 " + root + "/tools/mkdosdisk.py " + root + "/disks").c_str()) != 0) {
        std::puts("FAIL: disk image build failed");
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

    const std::string rom_path = root + "/partos/bin/partos.rom";
    const std::string hdd_path = root + "/disks/hdd-dos.img";
    const std::string kernel_map_path = root + "/partos/build/kernel.map";
    const std::string os_map_path = root + "/partos/build/os.map";
    const std::string shell_map_path = root + "/partos/build/shell_payload.map";

    const uint16_t kernel_base = read_area_addr(kernel_map_path, "_CODE");
    const uint16_t os_base = read_area_addr(os_map_path, "_CODE");
    symbol_map K(kernel_map_path);
    symbol_map O(os_map_path);
    symbol_map S(shell_map_path);

    const uint16_t sys_kernel = K.at("__sys_kernel");
    const uint16_t os_entry = O.at("__os_entry");
    const uint16_t os_drv_register_all = O.at("__drv_register_all");
    const uint16_t os_dev_init = O.at("__dev_init");
    const uint16_t os_dev_init_all = O.at("__dev_init_all");
    const uint16_t os_dev_probe_all = O.at("__dev_probe_all");
    const uint16_t os_syscall_init = O.at("_syscall_init");
    const uint16_t ir_enable = K.at("_ir_enable");
    const uint16_t os_bootstrap = O.at("_kernel_bootstrap");
    const uint16_t fat_mount = O.at("_fat_mount");
    const uint16_t fat_open = O.at("_fat_open");
    const uint16_t fat_read = O.at("_fat_read");
    const uint16_t process_load_com = O.at("_process_load_com");
    const uint16_t thread_exit = O.at("_thread_exit");
    const uint16_t ctc_init = O.at("ctc_init");
    const uint16_t boot_after_evt_create = O.at("__boot_after_evt_create");
    const uint16_t boot_try_sda = O.at("__boot_try_sda");
    const uint16_t boot_try_fd0 = O.at("__boot_try_fd0");
    const uint16_t boot_cleanup = O.at("__boot_cleanup");
    const uint16_t boot_exit = O.at("__boot_exit");
    const uint16_t boot_fs_addr = O.at("boot_fs$");
    const uint16_t fat_queue_event_addr = O.at("fat_queue_event$");
    const uint16_t fat_io_event_addr = O.at("fat_io_event$");
    const uint16_t boot_event_addr = (uint16_t)(boot_fs_addr - 0x22);
    const uint16_t boot_file_addr = (uint16_t)(boot_fs_addr + 30);
    const uint16_t fat_worker_thread_addr = (uint16_t)(fat_queue_event_addr + 4);
    const uint16_t fat_init_state_addr = (uint16_t)(fat_queue_event_addr + 6);
    const uint16_t fat_work_fs_addr = O.at("fat_work_fs$");
    const uint16_t fat_work_dev_addr = O.at("fat_work_dev$");
    const uint16_t fat_work_event_addr = O.at("fat_work_event$");
    const uint16_t hd_dev0_addr = O.at("hd_dev0");
    const uint16_t hd_io_ptr_addr = (uint16_t)(hd_dev0_addr + 0x14);
    const uint16_t hd_io_lba_addr = (uint16_t)(hd_dev0_addr + 0x16);
    const uint16_t hd_io_dev_addr = (uint16_t)(hd_dev0_addr + 0x18);
    const uint16_t hd_io_evt_addr = (uint16_t)(hd_dev0_addr + 0x1A);
    const uint16_t hd_io_misc_addr = (uint16_t)(hd_dev0_addr + 0x1C);
    const uint16_t thread_current_addr = K.at("_thread_current");
    const uint16_t thread_running_addr = K.at("_thread_first_running");
    const uint16_t thread_waiting_addr = K.at("_thread_first_waiting");
    const uint16_t shell_code_size = S.at("l__CODE");

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

    partner_crt emu(terminal_profile::vt52, root + "/partos/partos_shadow_nvram.bin");
    emu.load_rom(rom_path);
    emu.load_hdd(hdd_path);
    emu.reset();

    uint64_t guard = 0;
    while (emu.is_rom_enabled() || emu.get_current_pc() != PC_STAGE1_READY) {
        emu.tick();
        if (++guard > 50'000'000ULL) {
            std::printf("FAIL: never reached stage-1 ready state (pc=%04X rom=%d)\n",
                        emu.get_current_pc(), emu.is_rom_enabled() ? 1 : 0);
            return 1;
        }
    }

    static const std::array<uint8_t, 8> k_nvram = {
        0x00, 0x40, 0x80, 0x3F, 0x00, 0x00, 0x00, 0x02,
    };
    emu.seed_cmos_nvram(k_nvram.data(), k_nvram.size());
    emu.write_debug_memory(ADDR_MODEL, {0x00});
    emu.write_debug_memory(ADDR_NVRAM_CACHE,
                           std::vector<uint8_t>(k_nvram.begin(), k_nvram.end()));

    auto st = emu.capture_debug_cpu_state();
    st.sp = 0xBFFF;
    emu.apply_debug_cpu_state(st);
    emu.debug_set_pc(PC_BOOT_HD);

    bool hit_page0 = false;
    uint32_t page0_hits = 0;
    uint32_t page0_edges = 0;
    bool hit_kernel = false;
    bool hit_os_entry = false;
    bool hit_drv_register_all = false;
    bool hit_dev_init = false;
    bool hit_dev_init_all = false;
    bool hit_dev_probe_all = false;
    bool hit_syscall_init = false;
    bool hit_ir_enable = false;
    bool hit_bootstrap = false;
    bool hit_fat_mount = false;
    bool hit_fat_open = false;
    bool hit_fat_read = false;
    bool hit_process_load_com = false;
    bool hit_thread_exit = false;
    bool hit_ctc_init = false;
    bool hit_boot_after_evt_create = false;
    bool hit_boot_try_sda = false;
    bool hit_boot_try_fd0 = false;
    bool hit_boot_cleanup = false;
    bool hit_boot_exit = false;
    bool hit_shell_prompt = false;
    const bool stop_at_boot_try_fd0 = [] {
        const char *s = std::getenv("IDP_STOP_AT_BOOT_FD0");
        return s && s[0] && s[0] != '0';
    }();
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
    uint64_t tick_fat_mount = 0;
    uint64_t tick_fat_open = 0;
    uint64_t tick_fat_read = 0;
    uint64_t tick_process_load_com = 0;
    uint64_t tick_thread_exit = 0;
    uint64_t tick_ctc_init = 0;
    uint64_t tick_boot_after_evt_create = 0;
    uint64_t tick_boot_try_sda = 0;
    uint64_t tick_boot_try_fd0 = 0;
    uint64_t tick_boot_cleanup = 0;
    uint64_t tick_boot_exit = 0;
    uint64_t tick_shell_prompt = 0;
    bool rom_model_overwrite = false;
    uint16_t rom_model_overwrite_pc = 0;
    uint8_t rom_model_overwrite_val = 0;
    uint16_t rom_model_overwrite_hl = 0;
    uint16_t rom_model_overwrite_de = 0;
    uint16_t rom_model_overwrite_sp = 0;
    std::vector<page0_reentry> page0_reentries;
    std::deque<uint16_t> recent_pcs;
    std::deque<uint16_t> recent_shell_pcs;
    std::deque<sched_snapshot> recent_sched;
    std::vector<uint16_t> first_reentry_trail;
    std::vector<uint8_t> first_reentry_stack;
    bool captured_first_reentry = false;
    uint16_t shell_entry_abs = 0;

    emu.dbg_wtrap_addr = ADDR_MODEL;
    emu.dbg_wtrap_hi = ADDR_MODEL;
    emu.dbg_wtrap_hit = false;

    for (guard = 0; guard < RUN_TICK_LIMIT; ++guard) {
        const uint16_t prev_pc = emu.get_current_pc();
        const auto prev_cpu = emu.capture_debug_cpu_state();
        emu.tick();
        if (!rom_model_overwrite && emu.dbg_wtrap_hit) {
            rom_model_overwrite = true;
            rom_model_overwrite_pc = emu.dbg_wtrap_pc;
            rom_model_overwrite_val = emu.dbg_wtrap_val;
            rom_model_overwrite_hl = emu.dbg_wtrap_hl;
            rom_model_overwrite_de = emu.dbg_wtrap_de;
            rom_model_overwrite_sp = emu.dbg_wtrap_sp;
            emu.dbg_wtrap_hit = false;
        }
        const uint16_t pc = emu.get_current_pc();
        if (recent_pcs.empty() || recent_pcs.back() != pc) {
            if (recent_pcs.size() == 16)
                recent_pcs.pop_front();
            recent_pcs.push_back(pc);
        }
        if ((shell_entry_abs != 0) &&
            (pc >= shell_entry_abs) &&
            (pc < (uint16_t)(shell_entry_abs + shell_code_size))) {
            if (recent_shell_pcs.empty() || recent_shell_pcs.back() != pc) {
                if (recent_shell_pcs.size() == 32)
                    recent_shell_pcs.pop_front();
                recent_shell_pcs.push_back(pc);
            }
        }
        if (hit_fat_mount) {
            auto rd16_local = [](const std::vector<uint8_t> &v) -> uint16_t {
                return v.size() >= 2 ? (uint16_t)(v[0] | (uint16_t(v[1]) << 8)) : 0;
            };
            const auto current_cell = emu.read_debug_memory(thread_current_addr, 2);
            const auto running_cell = emu.read_debug_memory(thread_running_addr, 2);
            const auto waiting_cell = emu.read_debug_memory(thread_waiting_addr, 2);
            const auto boot_evt_cell = emu.read_debug_memory(boot_event_addr, 2);
            const auto fat_io_evt_cell = emu.read_debug_memory(fat_io_event_addr, 2);
            uint8_t boot_evt_state = 0xFF;
            uint8_t fat_io_evt_state = 0xFF;
            const uint16_t boot_evt_addr = rd16_local(boot_evt_cell);
            const uint16_t fat_io_evt_addr_live = rd16_local(fat_io_evt_cell);
            if (boot_evt_addr != 0) {
                const auto evt = emu.read_debug_memory((uint16_t)(boot_evt_addr + 4), 1);
                if (!evt.empty())
                    boot_evt_state = evt[0];
            }
            if (fat_io_evt_addr_live != 0) {
                const auto evt = emu.read_debug_memory((uint16_t)(fat_io_evt_addr_live + 4), 1);
                if (!evt.empty())
                    fat_io_evt_state = evt[0];
            }
            sched_snapshot snap{
                emu.get_tick_count(),
                pc,
                rd16_local(current_cell),
                rd16_local(running_cell),
                rd16_local(waiting_cell),
                boot_evt_state,
                fat_io_evt_state,
            };
            if (recent_sched.empty() ||
                recent_sched.back().current != snap.current ||
                recent_sched.back().running != snap.running ||
                recent_sched.back().waiting != snap.waiting ||
                recent_sched.back().boot_evt_state != snap.boot_evt_state ||
                recent_sched.back().io_evt_state != snap.io_evt_state) {
                if (recent_sched.size() == 32)
                    recent_sched.pop_front();
                recent_sched.push_back(snap);
            }
        }
        if (pc == KERNEL_ENTRY) {
            ++page0_hits;
            if (prev_pc != KERNEL_ENTRY) {
                ++page0_edges;
                if (hit_page0 && page0_reentries.size() < 8) {
                    page0_reentries.push_back(page0_reentry{
                        prev_pc,
                        prev_cpu.sp,
                        prev_cpu.i,
                        prev_cpu.iff1,
                        prev_cpu.iff2,
                        prev_cpu.halted,
                    });
                }
                if (hit_page0 && !captured_first_reentry) {
                    captured_first_reentry = true;
                    first_reentry_trail.assign(recent_pcs.begin(), recent_pcs.end());
                    first_reentry_stack = emu.read_debug_memory(prev_cpu.sp, 8);
                }
            }
            if (!hit_page0) {
                hit_page0 = true;
                tick_page0 = emu.get_tick_count();
                emu.dbg_wtrap_addr = 0x0004;
                emu.dbg_wtrap_hi = 0x0007;
                emu.dbg_wtrap_hit = false;
            }
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
        }
        if (!hit_fat_mount && pc == fat_mount) {
            hit_fat_mount = true;
            tick_fat_mount = emu.get_tick_count();
        }
        if (!hit_fat_open && pc == fat_open) {
            hit_fat_open = true;
            tick_fat_open = emu.get_tick_count();
        }
        if (!hit_fat_read && pc == fat_read) {
            hit_fat_read = true;
            tick_fat_read = emu.get_tick_count();
        }
        if (!hit_process_load_com && pc == process_load_com) {
            hit_process_load_com = true;
            tick_process_load_com = emu.get_tick_count();
        }
        if (shell_entry_abs == 0 && hit_process_load_com) {
            const auto process_first_bytes = emu.read_debug_memory(O.at("_process_first"), 2);
            const uint16_t process_addr =
                process_first_bytes.size() >= 2
                    ? (uint16_t)(process_first_bytes[0] |
                                 (uint16_t(process_first_bytes[1]) << 8))
                    : uint16_t(0);
            if (process_addr != 0) {
                const auto process_bytes = emu.read_debug_memory(process_addr, 15);
                if (process_bytes.size() >= 15) {
                    const uint16_t main_thread =
                        (uint16_t)(process_bytes[13] | (uint16_t(process_bytes[14]) << 8));
                    if (main_thread != 0) {
                        const auto thread_bytes = emu.read_debug_memory(main_thread, 25);
                        if (thread_bytes.size() >= 9 && thread_bytes[6] == 0xCD)
                            shell_entry_abs =
                                (uint16_t)(thread_bytes[7] | (uint16_t(thread_bytes[8]) << 8));
                    }
                }
            }
        }
        if (!hit_thread_exit && pc == thread_exit) {
            hit_thread_exit = true;
            tick_thread_exit = emu.get_tick_count();
        }
        if (!hit_ctc_init && pc == ctc_init) {
            hit_ctc_init = true;
            tick_ctc_init = emu.get_tick_count();
        }
        if (!hit_boot_after_evt_create && pc == boot_after_evt_create) {
            hit_boot_after_evt_create = true;
            tick_boot_after_evt_create = emu.get_tick_count();
        }
        if (!hit_boot_try_sda && pc == boot_try_sda) {
            hit_boot_try_sda = true;
            tick_boot_try_sda = emu.get_tick_count();
        }
        if (!hit_boot_try_fd0 && pc == boot_try_fd0) {
            hit_boot_try_fd0 = true;
            tick_boot_try_fd0 = emu.get_tick_count();
            if (stop_at_boot_try_fd0)
                break;
        }
        if (!hit_boot_cleanup && pc == boot_cleanup) {
            hit_boot_cleanup = true;
            tick_boot_cleanup = emu.get_tick_count();
        }
        if (!hit_boot_exit && pc == boot_exit) {
            hit_boot_exit = true;
            tick_boot_exit = emu.get_tick_count();
        }
        if (!hit_shell_prompt && (guard & 0x3FFu) == 0 && shell_prompt_seen(emu)) {
            hit_shell_prompt = true;
            tick_shell_prompt = emu.get_tick_count();
            break;
        }
    }

    std::printf("partos_full_boot milestones:\n");
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
    std::printf("  [%c] boot after evt   @ 0x%04X tick=%llu\n",
                hit_boot_after_evt_create ? 'x' : ' ', boot_after_evt_create,
                (unsigned long long)tick_boot_after_evt_create);
    std::printf("  [%c] boot try sda     @ 0x%04X tick=%llu\n",
                hit_boot_try_sda ? 'x' : ' ', boot_try_sda,
                (unsigned long long)tick_boot_try_sda);
    std::printf("  [%c] boot try fd0     @ 0x%04X tick=%llu\n",
                hit_boot_try_fd0 ? 'x' : ' ', boot_try_fd0,
                (unsigned long long)tick_boot_try_fd0);
    std::printf("  [%c] boot cleanup     @ 0x%04X tick=%llu\n",
                hit_boot_cleanup ? 'x' : ' ', boot_cleanup,
                (unsigned long long)tick_boot_cleanup);
    std::printf("  [%c] boot exit        @ 0x%04X tick=%llu\n",
                hit_boot_exit ? 'x' : ' ', boot_exit,
                (unsigned long long)tick_boot_exit);
    std::printf("  [%c] _fat_mount       @ 0x%04X tick=%llu\n",
                hit_fat_mount ? 'x' : ' ', fat_mount,
                (unsigned long long)tick_fat_mount);
    std::printf("  [%c] _fat_open        @ 0x%04X tick=%llu\n",
                hit_fat_open ? 'x' : ' ', fat_open,
                (unsigned long long)tick_fat_open);
    std::printf("  [%c] _fat_read        @ 0x%04X tick=%llu\n",
                hit_fat_read ? 'x' : ' ', fat_read,
                (unsigned long long)tick_fat_read);
    std::printf("  [%c] _process_load_com@ 0x%04X tick=%llu\n",
                hit_process_load_com ? 'x' : ' ', process_load_com,
                (unsigned long long)tick_process_load_com);
    std::printf("  [%c] _thread_exit     @ 0x%04X tick=%llu\n",
                hit_thread_exit ? 'x' : ' ', thread_exit,
                (unsigned long long)tick_thread_exit);
    std::printf("  [%c] ctc_init         @ 0x%04X tick=%llu\n",
                hit_ctc_init ? 'x' : ' ', ctc_init,
                (unsigned long long)tick_ctc_init);
    std::printf("  [%c] shell prompt      tick=%llu\n",
                hit_shell_prompt ? 'x' : ' ',
                (unsigned long long)tick_shell_prompt);

    if (!hit_page0 || !hit_kernel || !hit_os_entry || !hit_bootstrap || !hit_shell_prompt) {
        const auto cpu = emu.capture_debug_cpu_state();
        auto rd16 = [](const std::vector<uint8_t> &v) -> uint16_t {
            return v.size() >= 2 ? (uint16_t)(v[0] | (uint16_t(v[1]) << 8)) : 0xFFFFu;
        };
        const auto boot_hint = emu.read_debug_memory(0x0004, 4);
        const auto boot_model_cache = emu.read_debug_memory(K.at("__boot_model_cache"), 1);
        const auto sys_model = emu.read_debug_memory(O.at("__sys_model"), 1);
        const auto boot_event_cell = emu.read_debug_memory(boot_event_addr, 2);
        const auto boot_fs = emu.read_debug_memory(boot_fs_addr, 30);
        const auto boot_file = emu.read_debug_memory(boot_file_addr, 18);
        const auto fat_init_state = emu.read_debug_memory(fat_init_state_addr, 1);
        const auto fat_queue_event = emu.read_debug_memory(fat_queue_event_addr, 2);
        const auto fat_io_event = emu.read_debug_memory(fat_io_event_addr, 2);
        const auto fat_worker_thread = emu.read_debug_memory(fat_worker_thread_addr, 2);
        const auto fat_work_fs = emu.read_debug_memory(fat_work_fs_addr, 2);
        const auto fat_work_dev = emu.read_debug_memory(fat_work_dev_addr, 2);
        const auto fat_work_event = emu.read_debug_memory(fat_work_event_addr, 2);
        const auto process_first = emu.read_debug_memory(O.at("_process_first"), 2);
        const auto process_last_error = emu.read_debug_memory(O.at("_process_last_error"), 1);
        const uint16_t process_addr = rd16(process_first);
        const auto process_bytes =
            process_addr ? emu.read_debug_memory(process_addr, 15) : std::vector<uint8_t>{};
        const uint16_t sys_heap_addr = K.at("__sys_heap");
        const uint16_t usr_heap_addr = K.at("__usr_heap");
        const auto im2_spurious = emu.read_debug_memory(KERNEL_IM2_BASE + 0x00, 2);
        const auto im2_tick = emu.read_debug_memory(KERNEL_IM2_BASE + 0x8C, 2);
        const auto im2_vbl = emu.read_debug_memory(KERNEL_IM2_BASE + 0x8E, 2);
        const auto im2_hd = emu.read_debug_memory(KERNEL_IM2_BASE + 0x90, 2);
        const auto ir_refcnt = emu.read_debug_memory(K.at("ir_refcnt"), 1);
        const auto ir_armed = emu.read_debug_memory(K.at("ir_armed"), 1);
        const auto drv_first = emu.read_debug_memory(O.at("_drv_first"), 2);
        const auto cur_thread = emu.read_debug_memory(K.at("_thread_current"), 2);
        const auto run_head = emu.read_debug_memory(K.at("_thread_first_running"), 2);
        const auto wait_head = emu.read_debug_memory(K.at("_thread_first_waiting"), 2);
        const auto term_head = emu.read_debug_memory(K.at("_thread_first_terminated"), 2);
        const auto hd_dev = emu.read_debug_memory(hd_dev0_addr, 20);
        const auto hd_io_ptr = emu.read_debug_memory(hd_io_ptr_addr, 2);
        const auto hd_io_lba = emu.read_debug_memory(hd_io_lba_addr, 2);
        const auto hd_io_dev = emu.read_debug_memory(hd_io_dev_addr, 2);
        const auto hd_io_evt = emu.read_debug_memory(hd_io_evt_addr, 2);
        const auto hd_io_misc = emu.read_debug_memory(hd_io_misc_addr, 3);
        const auto cur_thread_addr =
            cur_thread.size() >= 2 ? uint16_t(cur_thread[0] | (uint16_t(cur_thread[1]) << 8))
                                   : uint16_t(0);
        const auto cur_thread_bytes =
            cur_thread_addr ? emu.read_debug_memory(cur_thread_addr, 25) : std::vector<uint8_t>{};
        const auto wait_thread_addr =
            wait_head.size() >= 2 ? uint16_t(wait_head[0] | (uint16_t(wait_head[1]) << 8))
                                  : uint16_t(0);
        const auto wait_thread_bytes =
            wait_thread_addr ? emu.read_debug_memory(wait_thread_addr, 25) : std::vector<uint8_t>{};
        const auto run_thread_addr =
            run_head.size() >= 2 ? uint16_t(run_head[0] | (uint16_t(run_head[1]) << 8))
                                 : uint16_t(0);
        const auto run_thread_bytes =
            run_thread_addr ? emu.read_debug_memory(run_thread_addr, 25) : std::vector<uint8_t>{};
        const auto wait2_thread_addr =
            wait_thread_bytes.size() >= 2 ? uint16_t(wait_thread_bytes[0] | (uint16_t(wait_thread_bytes[1]) << 8))
                                          : uint16_t(0);
        const auto wait2_thread_bytes =
            wait2_thread_addr ? emu.read_debug_memory(wait2_thread_addr, 25) : std::vector<uint8_t>{};
        const auto pc_bytes = emu.read_debug_memory(emu.get_current_pc(), 8);
        const auto sp_bytes = emu.read_debug_memory(cpu.sp, 8);
        const auto &ctc = emu.get_ctc();
        const auto &dma = emu.get_dma();
        std::printf("FAIL: ROM split boot stopped at pc=%04X sp=%04X iff1=%d iff2=%d halted=%d tick=%llu\n",
                    emu.get_current_pc(), cpu.sp,
                    cpu.iff1 ? 1 : 0, cpu.iff2 ? 1 : 0, cpu.halted ? 1 : 0,
                    (unsigned long long)emu.get_tick_count());
        std::printf("debug: page0 entries seen = %u\n", page0_hits);
        std::printf("debug: page0 transitions = %u\n", page0_edges);
        std::printf("debug: page0 boot bytes @0004 =");
        for (uint8_t v : boot_hint)
            std::printf(" %02X", v);
        std::printf("\n");
        std::printf("debug: boot model cache @%04X = %02X\n",
                    K.at("__boot_model_cache"),
                    boot_model_cache.empty() ? 0xFF : boot_model_cache[0]);
        std::printf("debug: __sys_model @%04X = %02X\n",
                    O.at("__sys_model"), sys_model.empty() ? 0xFF : sys_model[0]);
        std::printf("debug: boot_event cell @%04X = %04X\n",
                    boot_event_addr, rd16(boot_event_cell));
        if (rd16(boot_event_cell) != 0) {
            const auto boot_evt = emu.read_debug_memory(rd16(boot_event_cell), 5);
            if (boot_evt.size() >= 5) {
                std::printf("debug: boot_event @%04X next=%04X owner=%04X state=%02X\n",
                            rd16(boot_event_cell),
                            uint16_t(boot_evt[0] | (uint16_t(boot_evt[1]) << 8)),
                            uint16_t(boot_evt[2] | (uint16_t(boot_evt[3]) << 8)),
                            boot_evt[4]);
            }
        }
        if (boot_fs.size() >= 30) {
            std::printf("debug: boot_fs dev=%04X lba=%04X mounted=%u status=%04X fatbits=%u spc=%u\n",
                        uint16_t(boot_fs[0] | (uint16_t(boot_fs[1]) << 8)),
                        uint16_t(boot_fs[2] | (uint16_t(boot_fs[3]) << 8)),
                        (unsigned)boot_fs[27],
                        uint16_t(boot_fs[28] | (uint16_t(boot_fs[29]) << 8)),
                        (unsigned)boot_fs[26],
                        (unsigned)boot_fs[24]);
        }
        if (boot_file.size() >= 18) {
            std::printf("debug: boot_file cluster=%04X size=%02X%04X status=%04X fs=%04X pos=%02X%04X\n",
                        uint16_t(boot_file[0] | (uint16_t(boot_file[1]) << 8)),
                        (unsigned)boot_file[5],
                        uint16_t(boot_file[2] | (uint16_t(boot_file[3]) << 8)),
                        uint16_t(boot_file[10] | (uint16_t(boot_file[11]) << 8)),
                        uint16_t(boot_file[12] | (uint16_t(boot_file[13]) << 8)),
                        (unsigned)boot_file[17],
                        uint16_t(boot_file[14] | (uint16_t(boot_file[15]) << 8)));
        }
        std::printf("debug: fat init=%02X queue_evt=%04X io_evt=%04X worker=%04X\n",
                    fat_init_state.empty() ? 0xFF : fat_init_state[0],
                    rd16(fat_queue_event), rd16(fat_io_event), rd16(fat_worker_thread));
        auto dump_event = [&](const char *name, uint16_t cell_addr, const std::vector<uint8_t> &cell) {
            std::printf("debug: %s cell @%04X = %04X\n", name, cell_addr, rd16(cell));
            if (rd16(cell) != 0) {
                const auto evt = emu.read_debug_memory(rd16(cell), 5);
                if (evt.size() >= 5) {
                    std::printf("debug: %s @%04X next=%04X owner=%04X state=%02X\n",
                                name, rd16(cell),
                                uint16_t(evt[0] | (uint16_t(evt[1]) << 8)),
                                uint16_t(evt[2] | (uint16_t(evt[3]) << 8)),
                                evt[4]);
                }
            }
        };
        dump_event("fat_queue_event", fat_queue_event_addr, fat_queue_event);
        dump_event("fat_io_event", fat_io_event_addr, fat_io_event);
        std::printf("debug: fat worker scratch fs=%04X dev=%04X event=%04X\n",
                    rd16(fat_work_fs), rd16(fat_work_dev), rd16(fat_work_event));
        std::printf("debug: process first=%04X last_error=%02X\n",
                    rd16(process_first),
                    process_last_error.empty() ? 0xFF : process_last_error[0]);
        if (!process_bytes.empty()) {
            std::printf("debug: process raw =");
            for (uint8_t v : process_bytes)
                std::printf(" %02X", v);
            std::printf("\n");
        }
        auto dump_heap = [&](const char *name, uint16_t addr) {
            std::printf("debug: %s heap walk\n", name);
            uint16_t cur = addr;
            for (int i = 0; i < 8 && cur != 0; ++i) {
                const auto blk = emu.read_debug_memory(cur, 9);
                if (blk.size() < 9)
                    break;
                const uint16_t next = uint16_t(blk[0] | (uint16_t(blk[1]) << 8));
                const uint16_t owner = uint16_t(blk[2] | (uint16_t(blk[3]) << 8));
                const uint8_t stat = blk[4];
                const uint16_t size = uint16_t(blk[5] | (uint16_t(blk[6]) << 8));
                const uint16_t dtor = uint16_t(blk[7] | (uint16_t(blk[8]) << 8));
                std::printf("  %04X next=%04X owner=%04X stat=%02X size=%04X dtor=%04X\n",
                            cur, next, owner, stat, size, dtor);
                if (next == 0 || next == cur)
                    break;
                cur = next;
            }
        };
        dump_heap("sys", sys_heap_addr);
        dump_heap("usr", usr_heap_addr);
        std::printf("debug: ir_refcnt=%02X ir_armed=%02X\n",
                    ir_refcnt.empty() ? 0xFF : ir_refcnt[0],
                    ir_armed.empty() ? 0xFF : ir_armed[0]);
        std::printf("debug: _drv_first=%04X current=%04X running=%04X waiting=%04X terminated=%04X\n",
                    rd16(drv_first), cur_thread_addr, rd16(run_head),
                    rd16(wait_head), rd16(term_head));
        if (shell_entry_abs != 0) {
            const uint16_t shell_vars_addr = (uint16_t)(shell_entry_abs + 0x108);
            const auto shell_vars = emu.read_debug_memory(shell_vars_addr, 16);
            std::printf("debug: shell entry=%04X vars@%04X =",
                        shell_entry_abs, shell_vars_addr);
            for (uint8_t v : shell_vars)
                std::printf(" %02X", v);
            std::printf("\n");
        }
        if (cur_thread_bytes.size() >= 25) {
            const uint16_t t_next = uint16_t(cur_thread_bytes[0] | (uint16_t(cur_thread_bytes[1]) << 8));
            const uint16_t t_owner = uint16_t(cur_thread_bytes[2] | (uint16_t(cur_thread_bytes[3]) << 8));
            const uint16_t t_sp = uint16_t(cur_thread_bytes[4] | (uint16_t(cur_thread_bytes[5]) << 8));
            const uint16_t t_wait = uint16_t(cur_thread_bytes[16] | (uint16_t(cur_thread_bytes[17]) << 8));
            const uint16_t t_data = uint16_t(cur_thread_bytes[22] | (uint16_t(cur_thread_bytes[23]) << 8));
            const uint8_t t_numev = cur_thread_bytes[18];
            const uint8_t t_state = cur_thread_bytes[19];
            const uint8_t t_bank = cur_thread_bytes[24];
            std::printf("debug: current thread next=%04X owner=%04X sp=%04X wait=%04X numev=%u state=%u data=%04X bank=%u\n",
                        t_next, t_owner, t_sp, t_wait, (unsigned)t_numev,
                        (unsigned)t_state, t_data, (unsigned)t_bank);
            std::printf("debug: current thread raw =");
            for (uint8_t v : cur_thread_bytes)
                std::printf(" %02X", v);
            std::printf("\n");
        }
        if (run_thread_bytes.size() >= 25) {
            const uint16_t t_next = uint16_t(run_thread_bytes[0] | (uint16_t(run_thread_bytes[1]) << 8));
            const uint16_t t_sp = uint16_t(run_thread_bytes[4] | (uint16_t(run_thread_bytes[5]) << 8));
            const uint8_t t_state = run_thread_bytes[19];
            std::printf("debug: running head @%04X next=%04X sp=%04X state=%u\n",
                        run_thread_addr, t_next, t_sp, (unsigned)t_state);
            std::printf("debug: running head raw =");
            for (uint8_t v : run_thread_bytes)
                std::printf(" %02X", v);
            std::printf("\n");
        }
        if (run_thread_bytes.size() >= 2) {
            const uint16_t run_next_addr =
                uint16_t(run_thread_bytes[0] | (uint16_t(run_thread_bytes[1]) << 8));
            if (run_next_addr != 0) {
                const auto run_next_bytes = emu.read_debug_memory(run_next_addr, 25);
                if (run_next_bytes.size() >= 25) {
                    const uint16_t t_next =
                        uint16_t(run_next_bytes[0] | (uint16_t(run_next_bytes[1]) << 8));
                    const uint16_t t_sp =
                        uint16_t(run_next_bytes[4] | (uint16_t(run_next_bytes[5]) << 8));
                    const uint8_t t_state = run_next_bytes[19];
                    std::printf("debug: running next @%04X next=%04X sp=%04X state=%u\n",
                                run_next_addr, t_next, t_sp, (unsigned)t_state);
                    std::printf("debug: running next raw =");
                    for (uint8_t v : run_next_bytes)
                        std::printf(" %02X", v);
                    std::printf("\n");
                    const uint16_t run_next2_addr =
                        uint16_t(run_next_bytes[0] | (uint16_t(run_next_bytes[1]) << 8));
                    if (run_next2_addr != 0) {
                        const auto run_next2_bytes = emu.read_debug_memory(run_next2_addr, 25);
                        if (run_next2_bytes.size() >= 25) {
                            const uint16_t t_next =
                                uint16_t(run_next2_bytes[0] | (uint16_t(run_next2_bytes[1]) << 8));
                            const uint16_t t_sp =
                                uint16_t(run_next2_bytes[4] | (uint16_t(run_next2_bytes[5]) << 8));
                            const uint8_t t_state = run_next2_bytes[19];
                            std::printf("debug: running next2 @%04X next=%04X sp=%04X state=%u\n",
                                        run_next2_addr, t_next, t_sp, (unsigned)t_state);
                            std::printf("debug: running next2 raw =");
                            for (uint8_t v : run_next2_bytes)
                                std::printf(" %02X", v);
                            std::printf("\n");
                        }
                    }
                }
            }
        }
        if (wait_thread_bytes.size() >= 25) {
            const uint16_t t_wait = uint16_t(wait_thread_bytes[16] | (uint16_t(wait_thread_bytes[17]) << 8));
            const uint8_t t_numev = wait_thread_bytes[18];
            const uint8_t t_state = wait_thread_bytes[19];
            std::printf("debug: waiting thread @%04X wait=%04X numev=%u state=%u\n",
                        wait_thread_addr, t_wait, (unsigned)t_numev, (unsigned)t_state);
            std::printf("debug: waiting thread raw =");
            for (uint8_t v : wait_thread_bytes)
                std::printf(" %02X", v);
            std::printf("\n");
        }
        if (wait2_thread_bytes.size() >= 25) {
            const uint16_t t_wait = uint16_t(wait2_thread_bytes[16] | (uint16_t(wait2_thread_bytes[17]) << 8));
            const uint8_t t_numev = wait2_thread_bytes[18];
            const uint8_t t_state = wait2_thread_bytes[19];
            std::printf("debug: waiting thread2 @%04X wait=%04X numev=%u state=%u\n",
                        wait2_thread_addr, t_wait, (unsigned)t_numev, (unsigned)t_state);
            std::printf("debug: waiting thread2 raw =");
            for (uint8_t v : wait2_thread_bytes)
                std::printf(" %02X", v);
            std::printf("\n");
        }
        if (hd_dev.size() >= 20) {
            std::printf("debug: hd dev @%04X flags=%02X io_ptr=%04X lba=%04X io_dev=%04X io_evt=%04X count=%02X dir=%02X err=%02X\n",
                        hd_dev0_addr, hd_dev[8], rd16(hd_io_ptr), rd16(hd_io_lba), rd16(hd_io_dev),
                        rd16(hd_io_evt),
                        hd_io_misc.size() > 0 ? hd_io_misc[0] : 0xFF,
                        hd_io_misc.size() > 1 ? hd_io_misc[1] : 0xFF,
                        hd_io_misc.size() > 2 ? hd_io_misc[2] : 0xFF);
            const uint16_t hd_evt_addr = rd16(hd_io_evt);
            if (hd_evt_addr != 0) {
                const auto hd_evt = emu.read_debug_memory(hd_evt_addr, 5);
                if (hd_evt.size() >= 5)
                    std::printf("debug: hd evt @%04X next=%04X owner=%04X state=%02X\n",
                                hd_evt_addr, uint16_t(hd_evt[0] | (uint16_t(hd_evt[1]) << 8)),
                                uint16_t(hd_evt[2] | (uint16_t(hd_evt[3]) << 8)), hd_evt[4]);
            }
        }
        std::printf("debug: im2[00]=%04X im2[8C]=%04X im2[8E]=%04X im2[90]=%04X\n",
                    rd16(im2_spurious), rd16(im2_tick), rd16(im2_vbl), rd16(im2_hd));
        std::printf("debug: dma state=%02X int_state=%02X vec=%02X enabled=%d\n",
                    (unsigned)dma.state, (unsigned)dma.int_state,
                    (unsigned)dma.int_vector, dma.enabled ? 1 : 0);
        std::printf("debug: ctc ch2 ctrl=%02X const=%02X down=%02X wait=%d int=%02X vec=%02X\n",
                    ctc.chn[2].control, ctc.chn[2].constant, ctc.chn[2].down_counter,
                    ctc.chn[2].waiting_for_trigger ? 1 : 0,
                    ctc.chn[2].int_state, ctc.chn[2].int_vector);
        std::printf("debug: ctc ch3 ctrl=%02X const=%02X down=%02X wait=%d int=%02X vec=%02X\n",
                    ctc.chn[3].control, ctc.chn[3].constant, ctc.chn[3].down_counter,
                    ctc.chn[3].waiting_for_trigger ? 1 : 0,
                    ctc.chn[3].int_state, ctc.chn[3].int_vector);
        std::printf("debug: bytes @pc=%04X =", emu.get_current_pc());
        for (uint8_t v : pc_bytes)
            std::printf(" %02X", v);
        std::printf("\n");
        std::printf("debug: bytes @sp=%04X =", cpu.sp);
        for (uint8_t v : sp_bytes)
            std::printf(" %02X", v);
        std::printf("\n");
        if (emu.dbg_wtrap_hit) {
            std::printf("debug: page0 hint overwrite pc=%04X value=%02X hl=%04X de=%04X sp=%04X\n",
                        emu.dbg_wtrap_pc, emu.dbg_wtrap_val, emu.dbg_wtrap_hl,
                        emu.dbg_wtrap_de, emu.dbg_wtrap_sp);
        } else {
            std::printf("debug: no page0 hint overwrite trapped after kernel entry\n");
        }
        if (rom_model_overwrite) {
            std::printf("debug: rom model overwrite pc=%04X value=%02X hl=%04X de=%04X sp=%04X\n",
                        rom_model_overwrite_pc, rom_model_overwrite_val,
                        rom_model_overwrite_hl, rom_model_overwrite_de,
                        rom_model_overwrite_sp);
        } else {
            std::printf("debug: no rom model overwrite trapped before kernel entry\n");
        }
        if (!page0_reentries.empty()) {
            std::printf("debug: page0 re-entry trail\n");
            for (const auto &r : page0_reentries) {
                std::printf("  from=%04X sp=%04X i=%02X iff=%d/%d halted=%d\n",
                            r.from_pc, r.from_sp, r.i,
                            r.iff1 ? 1 : 0, r.iff2 ? 1 : 0, r.halted ? 1 : 0);
            }
        }
        if (!first_reentry_trail.empty()) {
            std::printf("debug: first re-entry recent pcs =");
            for (uint16_t v : first_reentry_trail)
                std::printf(" %04X", v);
            std::printf("\n");
        }
        if (!first_reentry_stack.empty()) {
            std::printf("debug: first re-entry stack @%04X =",
                        page0_reentries.empty() ? 0 : page0_reentries.front().from_sp);
            for (uint8_t v : first_reentry_stack)
                std::printf(" %02X", v);
            std::printf("\n");
        }
        if (!recent_shell_pcs.empty()) {
            std::printf("debug: recent shell pcs =");
            for (uint16_t v : recent_shell_pcs)
                std::printf(" %04X", v);
            std::printf("\n");
        }
        if (emu.dbg_im2_ack_count != 0) {
            const uint32_t total = emu.dbg_im2_ack_count;
            const uint32_t start = total > 8 ? (total - 8) : 0;
            std::printf("debug: recent im2 ack vectors");
            for (uint32_t n = start; n < total; ++n) {
                const uint32_t idx = n & 0x7u;
                std::printf(" [%u]=%02X@%04X",
                            n,
                            emu.dbg_im2_ack_vectors[idx],
                            emu.dbg_im2_ack_pcs[idx]);
            }
            std::printf("\n");
        }
        if (emu.dbg_irref_count != 0) {
            const uint32_t total = emu.dbg_irref_count;
            const uint32_t start = total > 16 ? (total - 16) : 0;
            std::printf("debug: recent ir_refcnt writes");
            for (uint32_t n = start; n < total; ++n) {
                const uint32_t idx = n & 0xFu;
                std::printf(" [%u]=%02X@%04X t=%llu sp=%04X stk=%04X/%04X",
                            n,
                            emu.dbg_irref_values[idx],
                            emu.dbg_irref_pcs[idx],
                            (unsigned long long)emu.dbg_irref_ticks[idx],
                            emu.dbg_irref_sps[idx],
                            emu.dbg_irref_stack0[idx],
                            emu.dbg_irref_stack1[idx]);
            }
            std::printf("\n");
        }
        if (!recent_sched.empty()) {
            std::printf("debug: recent scheduler snapshots\n");
            for (const auto &snap : recent_sched) {
                std::printf("  t=%llu pc=%04X current=%04X running=%04X waiting=%04X boot_evt=%02X io_evt=%02X\n",
                            (unsigned long long)snap.tick, snap.pc,
                            snap.current, snap.running, snap.waiting,
                            snap.boot_evt_state, snap.io_evt_state);
            }
        }
        std::printf("debug: terminal\n%s\n", emu.dump_terminal_text().c_str());
        std::printf("debug: raw\n%s\n", emu.dump_raw_serial_text().c_str());
        return 1;
    }

    std::printf("PASS: ROM boot reached the shell prompt in %llu ticks\n",
                (unsigned long long)emu.get_tick_count());
    return 0;
}
