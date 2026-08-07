// probe_shell_command.cpp
//
// Lightweight PartOS shell probe:
//   - boots straight to the shell prompt using the same staged handoff as the
//     existing full-boot probe
//   - injects one scripted command
//   - runs for a bounded number of ticks or until the prompt returns
//   - prints the final terminal/raw output so shell app regressions are quick
//     to inspect without the heavyweight tracing probe
//
// 2026-06-28   tstih

#include "partner_crt.hpp"

#include <array>
#include <algorithm>
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
constexpr uint16_t PC_BOOT_HD = 0x2052;
constexpr uint16_t ADDR_MODEL = 0xDE0E;
constexpr uint16_t ADDR_NVRAM_CACHE = 0xDE0F;
constexpr uint64_t BOOT_TICK_LIMIT = 40'000'000ULL;
constexpr uint64_t DEFAULT_KEY_TICKS = 4000ULL;
constexpr uint64_t DEFAULT_POST_TICKS = 2'000'000ULL;
constexpr uint16_t APP_RUNTIME_DATA_OFF = 0x0F19;
constexpr uint16_t APP_RUNTIME_EVENT_OFF = APP_RUNTIME_DATA_OFF + 4;
constexpr uint16_t FATFS_ALLOC_HINT_OFF = 22;
constexpr uint16_t FATFILE_FIRST_CLUSTER_OFF = 0;
constexpr uint16_t FATFILE_FS_OFF = 12;
constexpr uint16_t FATFILE_POS_OFF = 14;
constexpr uint16_t FATDIRINFO_STATUS_OFF = 10;
constexpr uint16_t FATDIRINFO_INDEX_OFF = 12;
constexpr uint16_t FATDIRINFO_NAME_OFF = 14;
constexpr uint16_t FATDIRINFO_SIZE = 25;
constexpr uint16_t FAT_EBUSY_VALUE = 0xfffd;
constexpr uint16_t FATREQ_OP_OFF = 4;
constexpr uint16_t FATREQ_BYTES_OFF = 14;
constexpr uint16_t FATREQ_ARG_OFF = 16;

struct symbol_map {
    std::map<std::string, std::vector<uint16_t>> syms;
    std::vector<std::pair<uint16_t, std::string>> by_addr;

    explicit symbol_map(const std::string &path)
    {
        std::ifstream f(path);
        std::string line;
        const std::regex re("([0-9A-Fa-f]{8})\\s+([_A-Za-z.][_A-Za-z0-9$.]*)");

        while (std::getline(f, line)) {
            for (auto it = std::sregex_iterator(line.begin(), line.end(), re);
                 it != std::sregex_iterator(); ++it) {
                const uint16_t a =
                    (uint16_t)std::stoul((*it)[1].str(), nullptr, 16);
                const std::string name = (*it)[2].str();
                syms[name].push_back(a);
                by_addr.push_back({a, name});
            }
        }
        for (auto &kv : syms) {
            std::sort(kv.second.begin(), kv.second.end());
            kv.second.erase(std::unique(kv.second.begin(), kv.second.end()),
                            kv.second.end());
        }
        std::sort(by_addr.begin(), by_addr.end());
        by_addr.erase(std::unique(by_addr.begin(), by_addr.end()), by_addr.end());
    }

    uint16_t at(const std::string &name, size_t idx = 0) const
    {
        auto it = syms.find(name);

        if (it == syms.end() || idx >= it->second.size()) {
            std::printf("FATAL: symbol '%s'[%zu] not in map\n",
                        name.c_str(), idx);
            std::exit(2);
        }
        return it->second[idx];
    }

    std::string nearest(uint16_t addr) const
    {
        std::string best = "?";
        uint16_t best_addr = 0;

        for (const auto &it : by_addr) {
            if (it.first > addr)
                break;
            best_addr = it.first;
            best = it.second;
        }
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%s+0x%X", best.c_str(),
                      (unsigned)(addr - best_addr));
        return std::string(buf);
    }
};

static uint16_t rd16(const std::vector<uint8_t> &v, size_t off = 0)
{
    return (v.size() >= (off + 2))
               ? (uint16_t)(v[off] | (uint16_t(v[off + 1]) << 8))
               : uint16_t(0);
}

static std::string bytes_hex(const std::vector<uint8_t> &v, size_t max_count = 16)
{
    std::string out;
    char buf[8];
    const size_t count = std::min(v.size(), max_count);

    for (size_t i = 0; i < count; ++i) {
        if (!out.empty())
            out.push_back(' ');
        std::snprintf(buf, sizeof(buf), "%02X", (unsigned)v[i]);
        out += buf;
    }
    return out;
}

static std::string read_cstr_bounded(const partner_crt &emu,
                                     uint16_t addr,
                                     size_t cap = 16)
{
    std::string out;
    const auto bytes = emu.read_debug_memory(addr, cap);

    for (uint8_t b : bytes) {
        if (b == 0)
            break;
        out.push_back((char)b);
    }
    return out;
}

static void dump_heap(const char *label,
                      const partner_crt &emu,
                      uint16_t heap,
                      size_t limit)
{
    std::vector<uint16_t> seen;
    uint16_t block = heap;
    size_t count = 0;

    std::printf("%s heap @%04X\n", label, heap);
    while (block != 0 && count < limit) {
        if (std::find(seen.begin(), seen.end(), block) != seen.end()) {
            std::printf("  cycle at %04X\n", block);
            return;
        }
        seen.push_back(block);
        const auto hdr = emu.read_debug_memory(block, 9);
        if (hdr.size() < 9) {
            std::printf("  %04X <short read>\n", block);
            return;
        }
        const uint16_t next = rd16(hdr, 0);
        const uint16_t owner = rd16(hdr, 2);
        const uint8_t stat = hdr[4];
        const uint16_t size = rd16(hdr, 5);
        const uint16_t dtor = rd16(hdr, 7);

        std::printf("  %02zu: blk=%04X next=%04X owner=%04X stat=%02X size=%04X dtor=%04X\n",
                    count, block, next, owner, stat, size, dtor);
        block = next;
        ++count;
    }
    if (block != 0)
        std::printf("  ... truncated after %zu blocks, next=%04X\n", count, block);
}

static uint64_t env_u64_or(const char *name, uint64_t fallback)
{
    const char *s = std::getenv(name);

    if (s == nullptr || *s == '\0')
        return fallback;
    const unsigned long long parsed = std::strtoull(s, nullptr, 10);
    return parsed == 0ULL ? fallback : (uint64_t)parsed;
}

static std::string lowercase_ascii(std::string s);

static std::string command_stem(const std::string &command)
{
    std::string out;

    for (char ch : command) {
        if (ch == '\r' || ch == '\n' || ch == ' ')
            break;
        if (ch >= 'A' && ch <= 'Z')
            out.push_back((char)(ch - 'A' + 'a'));
        else
            out.push_back(ch);
    }
    return out;
}

static std::string command_target_stem(const std::string &command)
{
    const char *env = std::getenv("IDP_APP_STEM");

    if (env != nullptr && *env != '\0')
        return lowercase_ascii(env);

    std::string stem;
    std::string line;
    for (char ch : command) {
        if (ch == '\r' || ch == '\n') {
            const std::string candidate = command_stem(line);
            if (!candidate.empty())
                stem = candidate;
            line.clear();
            continue;
        }
        line.push_back(ch);
    }
    const std::string candidate = command_stem(line);
    if (!candidate.empty())
        stem = candidate;
    return stem.empty() ? command_stem(command) : stem;
}

static std::string uppercase_ascii(std::string s)
{
    for (char &ch : s) {
        if (ch >= 'a' && ch <= 'z')
            ch = (char)(ch - 'a' + 'A');
    }
    return s;
}

static std::string lowercase_ascii(std::string s)
{
    for (char &ch : s) {
        if (ch >= 'A' && ch <= 'Z')
            ch = (char)(ch - 'A' + 'a');
    }
    return s;
}

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

static bool text_ends_at_prompt(const std::string &text)
{
    if (text.size() < 2 || text.compare(text.size() - 2, 2, "> ") != 0)
        return false;

    const size_t nl = text.rfind('\n');
    const size_t line_start = (nl == std::string::npos) ? 0 : nl + 1;

    for (size_t i = line_start; i + 2 < text.size(); ++i) {
        if (text[i] == ' ')
            return false;
    }
    return true;
}

static bool prompt_returned(const std::string &term, const std::string &raw)
{
    if (text_ends_at_prompt(raw))
        return true;
    return text_ends_at_prompt(term);
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

struct console_write_sample {
    uint64_t tick = 0;
    uint16_t thread = 0;
    uint16_t process = 0;
    uint16_t ret = 0;
    uint16_t sp = 0;
    uint16_t hl = 0;
    uint16_t de = 0;
    std::string pname;
    std::string bytes;
    std::string stack;
};

struct fat_write_call_sample {
    uint64_t tick = 0;
    uint16_t pc = 0;
    uint16_t file = 0;
    uint16_t buf = 0;
    uint16_t first_cluster = 0;
    uint16_t size_lo = 0;
    uint16_t size_hi = 0;
    uint8_t attr = 0;
    uint16_t status = 0;
    uint16_t fs = 0;
    uint16_t pos_lo = 0;
    uint16_t pos_hi = 0;
    std::string file_bytes;
};

struct readdir_sample {
    uint64_t tick = 0;
    uint16_t call_pc = 0;
    uint16_t info = 0;
    uint16_t path = 0;
    uint16_t first_cluster = 0;
    uint16_t size_lo = 0;
    uint16_t size_hi = 0;
    uint16_t status = 0;
    uint16_t index = 0;
    uint8_t attr = 0;
    std::string path_text;
    std::string info_bytes;
    std::string name_bytes;
};

struct ls_name_sample {
    uint64_t tick = 0;
    uint16_t pc = 0;
    uint16_t path = 0;
    uint16_t name = 0;
    uint16_t dirinfo = 0;
    uint16_t ret = 0;
    uint16_t sp = 0;
    uint16_t index = 0;
    uint16_t first_cluster = 0;
    uint16_t size_lo = 0;
    uint16_t size_hi = 0;
    uint8_t attr = 0;
    std::string name_bytes;
    std::string after_bytes;
    std::string dirinfo_bytes;
};

} // namespace

int main(int argc, char **argv)
{
    std::string root = ".";
    std::string command = "ls\r";
#ifdef IDP_SOURCE_ROOT
    root = IDP_SOURCE_ROOT;
#endif
    if (argc > 1)
        root = argv[1];
    if (argc > 2)
        command = argv[2];

    if (std::getenv("IDP_SKIP_BUILD") == nullptr && !build_all(root))
        return 1;

    const std::string rom_path = root + "/partos/bin/partos.rom";
    const std::string hdd_path = root + "/disks/hdd-dos.img";
    const std::string kernel_map_path = root + "/partos/build/kernel.map";
    const std::string os_map_path = root + "/partos/build/os.map";
    const std::string shell_map_path = root + "/partos/build/shell_payload.map";
    const std::string app_stem = command_target_stem(command);
    const std::string expected_proc_name = lowercase_ascii(app_stem);
    const std::string app_xld_map_path =
        root + "/partos/build/" + app_stem + "_xld.map";
    const std::string app_payload_map_path =
        root + "/partos/build/" + app_stem + "_payload.map";
    symbol_map K(kernel_map_path);
    symbol_map O(os_map_path);
    symbol_map S(shell_map_path);
    const bool have_xld_map = std::ifstream(app_xld_map_path).good();
    const bool have_payload_map = std::ifstream(app_payload_map_path).good();
    const bool have_app_map = have_xld_map || have_payload_map;
    const std::string app_map_path =
        have_xld_map ? app_xld_map_path : app_payload_map_path;
    const symbol_map A = have_app_map ? symbol_map(app_map_path) : symbol_map(os_map_path);
    const uint16_t app_symbol_bias = have_xld_map ? 0x000c : 0x0000;

    const uint16_t pc_process_wait = O.at("_process_wait");
    const uint16_t pwait_target_addr = O.at("_process_wait_target_debug");
    const uint16_t pwait_hl_addr = O.at("_process_wait_hl_debug");
    const uint16_t pwait_de_addr = O.at("_process_wait_de_debug");
    const uint16_t syscall_service_addr = O.at("_syscall_service");
    const uint16_t pc_run_command = O.at("_partos_run_command");
    const uint16_t pc_write_console = O.at("_partos_write_console");
    const uint16_t pc_svc_query = O.at("_svc_query");
    const uint16_t pc_svc_register = O.at("_svc_register");
    const uint16_t pc_fat_worker_loop = O.at("fat_worker_loop$");
    const uint16_t pc_fat_write_call = O.at("_fat_write");
    const uint16_t pc_fat_handle_create = O.at("fat_handle_create$");
    const uint16_t pc_fat_handle_write = O.at("fat_handle_write$");
    const uint16_t pc_fat_finish_dirent = O.at("fat_finish_dirent$");
    const uint16_t pc_fat_alloc_chain = O.at("fat_alloc_chain_cluster$");
    const uint16_t pc_fat_find_free = O.at("fat_find_free_cluster$");
    const uint16_t pc_fat_set_entry = O.at("fat_set_fat_entry$");
    const uint16_t pc_fat_cluster_to_sector = O.at("fat_cluster_to_sector$");
    const uint16_t pc_fat_get_entry = O.at("fat_get_fat_entry$");
    const uint16_t pc_app_read_directory =
        (have_app_map && A.syms.count("_app_read_directory") != 0)
            ? A.at("_app_read_directory")
            : 0;
    const uint16_t pc_ls_write_name =
        (have_app_map && A.syms.count("_ls_write_name") != 0)
            ? A.at("_ls_write_name")
            : 0;
    auto optional_os_symbol = [&](const std::string &name) -> uint16_t {
        auto it = O.syms.find(name);
        return (it == O.syms.end() || it->second.empty()) ? 0 : it->second[0];
    };
    const uint16_t fat_req_head_addr = optional_os_symbol("fat_req_head$");
    const uint16_t fat_req_tail_addr = optional_os_symbol("fat_req_tail$");
    const uint16_t fat_queue_event_addr = optional_os_symbol("fat_queue_event$");
    const uint16_t fat_io_event_addr = optional_os_symbol("fat_io_event$");
    const uint16_t fat_worker_thread_addr = optional_os_symbol("fat_worker_thread$");
    const uint16_t fat_init_state_addr = optional_os_symbol("fat_init_state$");
    const uint16_t fat_wait_evt_addr = optional_os_symbol("fat_wait_evt$");
    const uint16_t pc_thread_create = K.at("_thread_create");
    const uint16_t pc_tc_fail0 = K.at("tc_fail0$");
    const uint16_t pc_tc_fail1 = K.at("tc_fail1$");
    const uint16_t pc_evt_set = K.at("_evt_set");
    const uint16_t pc_evt_destroy = K.at("_evt_destroy");

    partner_crt emu(terminal_profile::vt52,
                    root + "/partos/partos_shadow_nvram.bin");
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

    uint64_t hits_svc_register = 0;
    uint64_t first_svc_register_tick = 0;
    uint16_t first_svc_register_hl = 0;
    uint16_t first_svc_register_de = 0;
    std::string first_svc_register_name;
    std::string first_svc_register_name_bytes;
    std::string first_svc_register_table_bytes;

    while (true) {
        emu.tick();
        if (emu.get_current_pc() == pc_svc_register) {
            hits_svc_register++;
            if (first_svc_register_tick == 0) {
                const auto boot_st = emu.capture_debug_cpu_state();
                first_svc_register_tick = emu.get_tick_count();
                first_svc_register_hl = boot_st.hl;
                first_svc_register_de = boot_st.de;
                first_svc_register_name =
                    read_cstr_bounded(emu, boot_st.hl, 24);
                first_svc_register_name_bytes =
                    bytes_hex(emu.read_debug_memory(boot_st.hl, 16), 16);
                first_svc_register_table_bytes =
                    bytes_hex(emu.read_debug_memory(boot_st.de, 16), 16);
            }
        }
        if ((guard & 0x3FFu) == 0 && shell_prompt_seen(emu))
            break;
        if (++guard > BOOT_TICK_LIMIT) {
            std::printf("FAIL: never reached shell prompt (pc=%04X tick=%llu)\n",
                        emu.get_current_pc(),
                        (unsigned long long)emu.get_tick_count());
            std::printf("terminal:\n%s\n", emu.dump_terminal_text().c_str());
            std::printf("raw:\n%s\n", emu.dump_raw_serial_text().c_str());
            return 1;
        }
    }

    const std::string initial_term = emu.dump_terminal_text();
    const uint64_t key_ticks = env_u64_or("IDP_KEY_TICKS", DEFAULT_KEY_TICKS);
    const uint64_t post_ticks = env_u64_or("IDP_POST_TICKS", DEFAULT_POST_TICKS);
    const bool break_on_prompt = (std::getenv("IDP_KEEP_RUNNING_AFTER_PROMPT") == nullptr);
    const bool trace_console_writes =
        (std::getenv("IDP_TRACE_CONSOLE_WRITES") != nullptr);

    uint64_t hits_process_wait = 0;
    uint64_t hits_run_command = 0;
    uint64_t first_process_wait_tick = 0;
    uint64_t first_run_command_tick = 0;
    uint16_t first_wait_current = 0;
    uint16_t first_wait_running = 0;
    uint16_t first_wait_current_next = 0;
    std::array<uint16_t, 96> wait_pc_trace{};
    std::array<uint16_t, 96> wait_current_trace{};
    size_t wait_trace_len = 0;
    uint64_t child_current_hits = 0;
    uint64_t first_child_current_tick = 0;
    uint16_t first_child_pc = 0;
    uint64_t child_user_hits = 0;
    uint64_t first_child_user_tick = 0;
    uint16_t first_child_user_pc = 0;
    std::array<uint16_t, 128> child_user_pc_trace{};
    size_t child_user_pc_trace_len = 0;
    std::array<uint16_t, 64> child_pc_trace{};
    size_t child_pc_trace_len = 0;
    uint16_t child_wait_sp = 0;
    uint16_t child_wait_ptr = 0;
    uint8_t child_wait_num = 0;
    std::string child_wait_stack;
    uint16_t child_wait_event = 0;
    uint8_t child_wait_event_state = 0;
    std::string child_wait_bytes;
    std::array<uint16_t, 128> current_trace_threads{};
    std::array<uint16_t, 128> current_trace_pcs{};
    std::array<uint64_t, 128> current_trace_ticks{};
    size_t current_trace_len = 0;
    uint16_t last_current = 0xffff;
    uint64_t thread_create_hits = 0;
    uint16_t last_thread_create_hl = 0;
    uint16_t last_thread_create_de = 0;
    uint16_t last_thread_create_bank = 0;
    uint16_t last_thread_create_data = 0;
    uint64_t tc_fail0_hits = 0;
    uint64_t tc_fail1_hits = 0;
    bool saw_child_write_console = false;
    uint16_t child_write_hl = 0;
    uint16_t child_write_de = 0;
    std::string child_write_bytes;
    bool saw_child_path_wrapper = false;
    uint16_t child_path_wrapper_pc = 0;
    uint16_t child_path_wrapper_sp = 0;
    uint16_t child_path_wrapper_hl = 0;
    uint16_t child_path_wrapper_de = 0;
    uint16_t child_path_wrapper_bc = 0;
    std::string child_path_wrapper_stack;
    bool saw_child_path_precall = false;
    uint16_t child_path_precall_pc = 0;
    uint16_t child_path_precall_sp = 0;
    uint16_t child_path_precall_hl = 0;
    uint16_t child_path_precall_de = 0;
    uint16_t child_path_precall_bc = 0;
    std::string child_path_precall_stack;
    bool saw_child_path_postcall = false;
    uint16_t child_path_postcall_pc = 0;
    uint16_t child_path_postcall_sp = 0;
    uint16_t child_path_postcall_hl = 0;
    uint16_t child_path_postcall_de = 0;
    uint16_t child_path_postcall_bc = 0;
    std::string child_path_postcall_stack;
    std::array<uint16_t, 4> child_path_postcall_bcs{};
    std::array<uint16_t, 4> child_path_postcall_des{};
    std::array<uint16_t, 4> child_path_postcall_sps{};
    size_t child_path_postcall_count = 0;
    bool saw_child_path_return = false;
    uint16_t child_path_return_pc = 0;
    uint16_t child_path_return_sp = 0;
    uint16_t child_path_return_hl = 0;
    uint16_t child_path_return_de = 0;
    uint16_t child_path_return_bc = 0;
    std::string child_path_return_stack;
    std::array<uint16_t, 4> child_path_return_des{};
    std::array<uint16_t, 4> child_path_return_bcs{};
    std::array<uint16_t, 4> child_path_return_sps{};
    std::array<uint16_t, 4> child_path_return_events{};
    size_t child_path_return_count = 0;
    bool saw_child_fat_create = false;
    uint16_t child_fat_create_pc = 0;
    uint16_t child_fat_create_ret_pc = 0;
    uint16_t child_fat_create_caller_ret_pc = 0;
    uint16_t child_fat_create_sp = 0;
    uint16_t child_fat_create_hl = 0;
    uint16_t child_fat_create_de = 0;
    uint16_t child_fat_create_bc = 0;
    std::string child_fat_create_stack;
    std::string child_fat_create_caller_bytes;
    bool saw_child_fat_create_return = false;
    uint16_t child_fat_create_return_pc = 0;
    uint16_t child_fat_create_return_sp = 0;
    uint16_t child_fat_create_return_hl = 0;
    uint16_t child_fat_create_return_de = 0;
    uint16_t child_fat_create_return_bc = 0;
    uint16_t child_fat_create_return_ix = 0;
    uint16_t child_fat_create_return_iy = 0;
    std::string child_fat_create_return_stack;
    bool saw_child_fat_create_caller = false;
    uint16_t child_fat_create_caller_pc = 0;
    uint16_t child_fat_create_caller_sp = 0;
    uint16_t child_fat_create_caller_hl = 0;
    uint16_t child_fat_create_caller_de = 0;
    uint16_t child_fat_create_caller_bc = 0;
    std::string child_fat_create_caller_stack;
    uint64_t fat_worker_loop_hits = 0;
    uint64_t fat_worker_create_hits = 0;
    uint16_t fat_worker_create_pc = 0;
    uint16_t fat_worker_create_sp = 0;
    uint16_t fat_worker_create_req = 0;
    uint64_t fat_worker_create_tick = 0;
    std::string fat_worker_create_stack;
    std::string fat_worker_create_req_bytes;
    uint64_t fat_finish_hits = 0;
    uint16_t fat_finish_pc = 0;
    uint16_t fat_finish_sp = 0;
    uint16_t fat_finish_status = 0;
    uint16_t fat_finish_event = 0;
    uint16_t fat_finish_dirent = 0;
    uint8_t fat_finish_event_state = 0;
    std::string fat_finish_stack;
    std::array<uint16_t, 16> evt_set_events{};
    std::array<uint8_t, 16> evt_set_args{};
    std::array<uint8_t, 16> evt_set_states{};
    std::array<uint64_t, 16> evt_set_ticks{};
    size_t evt_set_count = 0;
    std::array<uint8_t, 16> target_evt_set_args{};
    std::array<uint8_t, 16> target_evt_set_states{};
    std::array<uint64_t, 16> target_evt_set_ticks{};
    size_t target_evt_set_count = 0;
    std::array<uint16_t, 8> evt_destroy_events{};
    std::array<uint64_t, 8> evt_destroy_ticks{};
    size_t evt_destroy_count = 0;
    bool saw_child_call_hl = false;
    uint16_t child_call_hl_pc = 0;
    uint16_t child_call_hl_hl = 0;
    bool saw_child_call_iy = false;
    uint16_t child_call_iy_pc = 0;
    uint16_t child_call_iy_iy = 0;
    bool saw_child_main = false;
    uint16_t child_main_pc = 0;
    bool saw_child_app_write_cstr = false;
    uint16_t child_app_write_cstr_pc = 0;
    uint16_t child_app_write_cstr_hl = 0;
    bool saw_child_svc_query = false;
    uint16_t child_svc_query_hl = 0;
    std::string child_svc_query_name;
    std::array<uint16_t, 8> child_svc_query_pcs{};
    std::array<uint16_t, 8> child_svc_query_hls{};
    std::array<std::string, 8> child_svc_query_names{};
    size_t child_svc_query_count = 0;
    std::array<uint16_t, 64> child_svc_pc_trace{};
    size_t child_svc_pc_trace_len = 0;
    bool saw_child_svc_found = false;
    uint16_t child_svc_found_pc = 0;
    uint16_t child_svc_found_hl = 0;
    bool saw_child_crt0_tail = false;
    uint16_t child_crt0_tail_pc = 0;
    uint16_t child_crt0_tail_hl = 0;
    uint16_t child_crt0_tail_de = 0;
    uint16_t child_crt0_tail_bc = 0;
    uint16_t child_crt0_tail_sp = 0;
    std::string child_crt0_tail_stack;
    std::string child_crt0_tail_hl_bytes;
    std::string child_crt0_tail_de_bytes;
    bool saw_child_boot_after_init = false;
    uint16_t child_boot_after_init_pc = 0;
    uint16_t child_boot_after_init_de = 0;
    uint16_t child_boot_after_init_hl = 0;
    uint16_t child_boot_after_init_iy = 0;
    bool saw_child_pa_init_after_rst = false;
    uint16_t child_pa_init_after_rst_pc = 0;
    uint16_t child_pa_init_after_rst_de = 0;
    uint16_t child_pa_init_after_rst_hl = 0;
    bool saw_child_pa_dead = false;
    uint16_t child_pa_dead_pc = 0;
    uint16_t child_pa_dead_sp = 0;
    std::string child_pa_dead_stack;
    uint16_t target_process = 0;
    uint16_t target_main_thread = 0;
    std::string target_name;
    uint16_t child_stack_block = 0;
    uint16_t child_stack_next_block = 0;
    std::vector<uint8_t> child_stack_next_hdr;
    uint32_t child_stack_next_hdr_changes = 0;
    bool captured_child_process_state = false;
    uint16_t child_process_ptr = 0;
    uint16_t child_process_cmd = 0;
    uint16_t child_process_env = 0;
    std::string child_process_cmd_text;
    std::string child_process_env_bytes;
    uint16_t child_libc_service = 0;
    std::string child_libc_name_bytes;
    uint16_t child_app_base = 0;
    uint16_t child_app_top = 0;
    std::vector<std::string> fat_worker_helper_trace;
    std::vector<console_write_sample> console_writes;
    std::vector<fat_write_call_sample> fat_write_calls;
    std::vector<readdir_sample> readdir_samples;
    std::vector<ls_name_sample> ls_name_samples;
    uint16_t pending_readdir_info = 0;
    uint16_t pending_readdir_path = 0;
    uint16_t pending_readdir_call_pc = 0;
    bool pending_readdir_saw_busy = false;
    uint16_t last_ls_path_ptr = 0;
    uint16_t last_ls_name_index = 0xffff;

    auto record_console_write = [&]() {
        if (!trace_console_writes ||
            emu.get_current_pc() != pc_write_console ||
            console_writes.size() >= 600) {
            return;
        }

        const auto st = emu.capture_debug_cpu_state();
        const uint16_t current =
            rd16(emu.read_debug_memory(K.at("_thread_current"), 2));
        const uint16_t process =
            current == 0
                ? 0
                : rd16(emu.read_debug_memory((uint16_t)(current + 22), 2));
        if (target_process != 0 && process != target_process) {
            return;
        }

        console_write_sample sample;
        sample.tick = emu.get_tick_count();
        sample.thread = current;
        sample.process = process;
        sample.ret = rd16(emu.read_debug_memory(st.sp, 2));
        sample.sp = st.sp;
        sample.hl = st.hl;
        sample.de = st.de;
        sample.pname =
            process == 0 ? "" : read_cstr_bounded(emu, (uint16_t)(process + 5), 8);
        sample.bytes =
            bytes_hex(emu.read_debug_memory(st.hl, st.de > 24 ? 24 : st.de),
                      st.de > 24 ? 24 : st.de);
        sample.stack = bytes_hex(emu.read_debug_memory(st.sp, 12), 12);
        console_writes.push_back(sample);
    };

    for (char ch : command) {
        if (ch == '\n') {
            for (uint64_t n = 0; n < post_ticks; ++n) {
                emu.tick();
                record_console_write();
                const std::string term = emu.dump_terminal_text();
                const std::string raw = emu.dump_raw_serial_text();
                if (prompt_returned(term, raw))
                    break;
            }
            continue;
        }
        emu.key_input((uint8_t)ch);
        for (uint64_t n = 0; n < key_ticks; ++n) {
            emu.tick();
            record_console_write();
        }
    }

    bool returned = false;
    for (uint64_t n = 0; n < post_ticks; ++n) {
        emu.tick();
        const uint16_t pc = emu.get_current_pc();
        record_console_write();
        if (first_process_wait_tick != 0 && wait_trace_len < wait_pc_trace.size()) {
            wait_pc_trace[wait_trace_len] = pc;
            wait_current_trace[wait_trace_len] =
                rd16(emu.read_debug_memory(K.at("_thread_current"), 2));
            wait_trace_len++;
        }

        if (pc == pc_process_wait) {
            hits_process_wait++;
            if (first_process_wait_tick == 0) {
                first_process_wait_tick = emu.get_tick_count();
                first_wait_current =
                    rd16(emu.read_debug_memory(K.at("_thread_current"), 2));
                first_wait_running =
                    rd16(emu.read_debug_memory(K.at("_thread_first_running"), 2));
                if (first_wait_current != 0) {
                    first_wait_current_next =
                        rd16(emu.read_debug_memory(first_wait_current, 2));
                }
            }
        }
        if (pc == pc_run_command) {
            hits_run_command++;
            if (first_run_command_tick == 0)
                first_run_command_tick = emu.get_tick_count();
        }
        if (pc == pc_svc_register) {
            hits_svc_register++;
            if (first_svc_register_tick == 0) {
                const auto st = emu.capture_debug_cpu_state();
                first_svc_register_tick = emu.get_tick_count();
                first_svc_register_hl = st.hl;
                first_svc_register_de = st.de;
                first_svc_register_name = read_cstr_bounded(emu, st.hl, 24);
                first_svc_register_name_bytes =
                    bytes_hex(emu.read_debug_memory(st.hl, 16), 16);
                first_svc_register_table_bytes =
                    bytes_hex(emu.read_debug_memory(st.de, 16), 16);
            }
        }
        if (pc == pc_thread_create) {
            const auto st = emu.capture_debug_cpu_state();
            const auto stack = emu.read_debug_memory(st.sp, 6);

            thread_create_hits++;
            last_thread_create_hl = st.hl;
            last_thread_create_de = st.de;
            last_thread_create_bank = stack.size() >= 4 ? rd16(stack, 2) : 0;
            last_thread_create_data = stack.size() >= 6 ? rd16(stack, 4) : 0;
        }
        if (pc == pc_tc_fail0)
            tc_fail0_hits++;
        if (pc == pc_tc_fail1)
            tc_fail1_hits++;
        {
            const uint16_t current =
                rd16(emu.read_debug_memory(K.at("_thread_current"), 2));
            const uint16_t fat_worker_thread =
                fat_worker_thread_addr == 0
                    ? 0
                    : rd16(emu.read_debug_memory(fat_worker_thread_addr, 2));
            if (pc == pc_fat_worker_loop)
                fat_worker_loop_hits++;
            if (pc == pc_fat_handle_create) {
                fat_worker_create_hits++;
                if (fat_worker_create_pc == 0) {
                    const auto st = emu.capture_debug_cpu_state();
                    fat_worker_create_pc = pc;
                    fat_worker_create_sp = st.sp;
                    fat_worker_create_req = st.de;
                    fat_worker_create_tick = emu.get_tick_count();
                    fat_worker_create_stack =
                        bytes_hex(emu.read_debug_memory(st.sp, 16), 16);
                    fat_worker_create_req_bytes =
                        bytes_hex(emu.read_debug_memory(st.de, 18), 18);
                }
            }
            if (pc == pc_fat_finish_dirent &&
                fat_worker_create_tick != 0 &&
                emu.get_tick_count() >= fat_worker_create_tick) {
                fat_finish_hits++;
                if (fat_finish_pc == 0) {
                    const auto st = emu.capture_debug_cpu_state();
                    fat_finish_pc = pc;
                    fat_finish_sp = st.sp;
                    fat_finish_status = st.de;
                    fat_finish_event =
                        rd16(emu.read_debug_memory(O.at("fat_work_event$"), 2));
                    fat_finish_dirent =
                        rd16(emu.read_debug_memory(O.at("fat_lookup_dirent$"), 2));
                    if (fat_finish_event != 0) {
                        const auto evt_bytes =
                            emu.read_debug_memory(fat_finish_event, 5);
                        if (evt_bytes.size() >= 5)
                            fat_finish_event_state = evt_bytes[4];
                    }
                    fat_finish_stack =
                        bytes_hex(emu.read_debug_memory(st.sp, 16), 16);
                }
            }
            if (pc == pc_evt_set && evt_set_count < evt_set_events.size()) {
                const auto st = emu.capture_debug_cpu_state();
                const auto arg = emu.read_debug_memory((uint16_t)(st.sp + 2), 1);
                const uint8_t arg0 = arg.empty() ? 0xff : arg[0];
                const uint8_t state0 =
                    st.hl == 0
                        ? 0xff
                        : emu.read_debug_memory((uint16_t)(st.hl + 4), 1).empty()
                              ? 0xff
                              : emu.read_debug_memory((uint16_t)(st.hl + 4), 1)[0];
                evt_set_events[evt_set_count] = st.hl;
                evt_set_args[evt_set_count] = arg0;
                evt_set_ticks[evt_set_count] = emu.get_tick_count();
                evt_set_states[evt_set_count] = state0;
                evt_set_count++;
                if ((child_wait_event != 0) &&
                    (st.hl == child_wait_event) &&
                    (target_evt_set_count < target_evt_set_args.size())) {
                    target_evt_set_args[target_evt_set_count] = arg0;
                    target_evt_set_states[target_evt_set_count] = state0;
                    target_evt_set_ticks[target_evt_set_count] =
                        emu.get_tick_count();
                    target_evt_set_count++;
                }
            }
            if (pc == pc_evt_destroy &&
                evt_destroy_count < evt_destroy_events.size()) {
                const auto st = emu.capture_debug_cpu_state();
                evt_destroy_events[evt_destroy_count] = st.hl;
                evt_destroy_ticks[evt_destroy_count] = emu.get_tick_count();
                evt_destroy_count++;
            }
            if (current != last_current) {
                last_current = current;
                if (current_trace_len < current_trace_threads.size()) {
                    current_trace_threads[current_trace_len] = current;
                    current_trace_pcs[current_trace_len] = pc;
                    current_trace_ticks[current_trace_len] = emu.get_tick_count();
                    current_trace_len++;
                }
            }
            if ((target_main_thread != 0) &&
                (current != 0) &&
                (current == fat_worker_thread) &&
                (fat_worker_helper_trace.size() < 256)) {
                const auto st = emu.capture_debug_cpu_state();
                char buf[192];
                auto push_worker_trace = [&](const char *text) {
                    if (fat_worker_helper_trace.empty() ||
                        fat_worker_helper_trace.back() != text) {
                        fat_worker_helper_trace.emplace_back(text);
                    }
                };

                if (pc == pc_fat_handle_write) {
                    const uint8_t op =
                        emu.read_debug_memory((uint16_t)(st.de + FATREQ_OP_OFF), 1).empty()
                            ? 0xff
                            : emu.read_debug_memory((uint16_t)(st.de + FATREQ_OP_OFF), 1)[0];
                    const uint16_t bytes =
                        rd16(emu.read_debug_memory((uint16_t)(st.de + FATREQ_BYTES_OFF), 2));
                    const uint16_t arg =
                        rd16(emu.read_debug_memory((uint16_t)(st.de + FATREQ_ARG_OFF), 2));
                    const uint16_t first_cluster =
                        rd16(emu.read_debug_memory((uint16_t)(arg + FATFILE_FIRST_CLUSTER_OFF), 2));
                    const uint16_t fs_ptr =
                        rd16(emu.read_debug_memory((uint16_t)(arg + FATFILE_FS_OFF), 2));
                    const uint16_t pos_secs =
                        rd16(emu.read_debug_memory((uint16_t)(arg + FATFILE_POS_OFF + 1), 2));
                    const uint16_t alloc_hint =
                        fs_ptr == 0
                            ? 0
                            : rd16(emu.read_debug_memory((uint16_t)(fs_ptr + FATFS_ALLOC_HINT_OFF), 2));
                    std::snprintf(buf, sizeof(buf),
                                  "tick=%llu handle_write pc=%04X sp=%04X req=%04X op=%02X bytes=%04X arg=%04X file_fc=%04X file_pos=%04X fs=%04X hint=%04X hl=%04X de=%04X bc=%04X",
                                  (unsigned long long)emu.get_tick_count(),
                                  pc, st.sp, st.de, (unsigned)op, bytes, arg,
                                  first_cluster, pos_secs, fs_ptr, alloc_hint,
                                  st.hl, st.de, st.bc);
                    push_worker_trace(buf);
                } else if (pc == pc_fat_alloc_chain) {
                    std::snprintf(buf, sizeof(buf),
                                  "tick=%llu alloc_chain pc=%04X sp=%04X prev=%04X hl=%04X de=%04X bc=%04X",
                                  (unsigned long long)emu.get_tick_count(),
                                  pc, st.sp, st.de, st.hl, st.de, st.bc);
                    push_worker_trace(buf);
                } else if (pc == pc_fat_find_free) {
                    const uint16_t fs_ptr =
                        rd16(emu.read_debug_memory(O.at("fat_work_fs$"), 2));
                    const uint16_t alloc_hint =
                        fs_ptr == 0
                            ? 0
                            : rd16(emu.read_debug_memory((uint16_t)(fs_ptr + FATFS_ALLOC_HINT_OFF), 2));
                    std::snprintf(buf, sizeof(buf),
                                  "tick=%llu find_free pc=%04X sp=%04X fs=%04X hint=%04X hl=%04X de=%04X bc=%04X",
                                  (unsigned long long)emu.get_tick_count(),
                                  pc, st.sp, fs_ptr, alloc_hint, st.hl, st.de, st.bc);
                    push_worker_trace(buf);
                } else if (pc == pc_fat_set_entry) {
                    std::snprintf(buf, sizeof(buf),
                                  "tick=%llu set_entry pc=%04X sp=%04X cluster=%04X value=%04X bc=%04X",
                                  (unsigned long long)emu.get_tick_count(),
                                  pc, st.sp, st.de, st.hl, st.bc);
                    push_worker_trace(buf);
                } else if (pc == pc_fat_get_entry) {
                    std::snprintf(buf, sizeof(buf),
                                  "tick=%llu get_entry pc=%04X sp=%04X cluster=%04X hl=%04X bc=%04X",
                                  (unsigned long long)emu.get_tick_count(),
                                  pc, st.sp, st.de, st.hl, st.bc);
                    push_worker_trace(buf);
                } else if (pc == pc_fat_cluster_to_sector) {
                    std::snprintf(buf, sizeof(buf),
                                  "tick=%llu cluster_to_sector pc=%04X sp=%04X cluster=%04X hl=%04X bc=%04X",
                                  (unsigned long long)emu.get_tick_count(),
                                  pc, st.sp, st.de, st.hl, st.bc);
                    push_worker_trace(buf);
                }
            }
            const uint16_t process0 =
                rd16(emu.read_debug_memory(O.at("_process_first"), 2));
            const uint16_t main0 =
                process0 == 0
                    ? 0
                    : rd16(emu.read_debug_memory((uint16_t)(process0 + 13), 2));
            const uint16_t process1 =
                process0 == 0 ? 0 : rd16(emu.read_debug_memory(process0, 2));
            const uint16_t main1 =
                process1 == 0
                    ? 0
                    : rd16(emu.read_debug_memory((uint16_t)(process1 + 13), 2));
            if (target_main_thread == 0) {
                const std::string name0 =
                    process0 == 0 ? "" : read_cstr_bounded(emu, (uint16_t)(process0 + 5), 8);
                const std::string name1 =
                    process1 == 0 ? "" : read_cstr_bounded(emu, (uint16_t)(process1 + 5), 8);
                if (!name0.empty() && name0 == expected_proc_name &&
                    main0 >= 0x0100) {
                    target_process = process0;
                    target_main_thread = main0;
                    target_name = name0;
                } else if (!name1.empty() && name1 == expected_proc_name &&
                           main1 >= 0x0100) {
                    target_process = process1;
                    target_main_thread = main1;
                    target_name = name1;
                }
            }
            if (target_main_thread != 0 && child_stack_block == 0) {
                uint16_t block =
                    rd16(emu.read_debug_memory(K.at("__usr_heap"), 2));

                while (block != 0) {
                    const auto hdr = emu.read_debug_memory(block, 9);
                    if (hdr.size() < 9) {
                        break;
                    }
                    if ((rd16(hdr, 2) == target_main_thread) && (hdr[4] == 0x01)) {
                        child_stack_block = block;
                        child_stack_next_block = rd16(hdr, 0);
                        if (child_stack_next_block != 0) {
                            child_stack_next_hdr =
                                emu.read_debug_memory(child_stack_next_block, 16);
                        }
                        break;
                    }
                    block = rd16(hdr, 0);
                }
            }
            const uint16_t entry0 =
                target_main_thread == 0
                    ? 0
                    : rd16(emu.read_debug_memory((uint16_t)(target_main_thread + 7), 2));
            uint16_t child_base = 0;
            uint16_t child_top = 0;

            if (have_app_map && entry0 >= A.at("_app_crt0_entry")) {
                child_base = (uint16_t)(entry0 - A.at("_app_crt0_entry"));
                child_top = (uint16_t)(child_base + A.at("l__CODE"));
                child_app_base = child_base;
                child_app_top = child_top;
            }

	            if (target_main_thread != 0 && current == target_main_thread) {
                child_current_hits++;
                if (first_child_current_tick == 0) {
                    first_child_current_tick = emu.get_tick_count();
                    first_child_pc = pc;
                }
                if (!captured_child_process_state && target_process != 0) {
                    child_process_ptr = rd16(
                        emu.read_debug_memory((uint16_t)(current + 22), 2));
                    child_process_cmd = rd16(
                        emu.read_debug_memory((uint16_t)(target_process + 15), 2));
                    child_process_env = rd16(
                        emu.read_debug_memory((uint16_t)(target_process + 17), 2));
                    child_process_cmd_text =
                        child_process_cmd == 0
                            ? ""
                            : read_cstr_bounded(emu, child_process_cmd, 64);
                    child_process_env_bytes =
                        child_process_env == 0
                            ? ""
                            : bytes_hex(emu.read_debug_memory(child_process_env, 16), 16);
                    child_libc_service =
                        rd16(emu.read_debug_memory(O.at("__svc_first"), 2));
                    child_libc_name_bytes =
                        child_libc_service == 0
                            ? ""
                            : bytes_hex(
                                  emu.read_debug_memory((uint16_t)(child_libc_service + 4),
                                                        16),
                                  16);
                    captured_child_process_state = true;
                }
                if (child_pc_trace_len < child_pc_trace.size() &&
                    (child_pc_trace_len == 0 ||
                     child_pc_trace[child_pc_trace_len - 1] != pc)) {
                    child_pc_trace[child_pc_trace_len++] = pc;
                }
                if (have_app_map && pc >= child_base && pc < child_top) {
                    child_user_hits++;
                    if (first_child_user_tick == 0) {
                        first_child_user_tick = emu.get_tick_count();
                        first_child_user_pc = pc;
                    }
                    if (child_user_pc_trace_len < child_user_pc_trace.size() &&
                        (child_user_pc_trace_len == 0 ||
                         child_user_pc_trace[child_user_pc_trace_len - 1] != pc)) {
                        child_user_pc_trace[child_user_pc_trace_len++] = pc;
                    }
                }
                if (!saw_child_call_hl && have_app_map &&
                    pc == child_base) {
                    const auto st = emu.capture_debug_cpu_state();
                    child_call_hl_pc = pc;
                    child_call_hl_hl = st.hl;
                    saw_child_call_hl = true;
                }
                if (!saw_child_call_iy && have_app_map &&
                    pc == child_base + 1) {
                    const auto st = emu.capture_debug_cpu_state();
                    child_call_iy_pc = pc;
                    child_call_iy_iy = st.iy;
                    saw_child_call_iy = true;
                }
                if (!saw_child_crt0_tail && have_app_map &&
                    pc >= child_base + 0x002a &&
                    pc <= child_base + 0x002c) {
                    const auto st = emu.capture_debug_cpu_state();
                    child_crt0_tail_pc = pc;
                    child_crt0_tail_hl = st.hl;
                    child_crt0_tail_de = st.de;
                    child_crt0_tail_bc = st.bc;
                    child_crt0_tail_sp = st.sp;
                    child_crt0_tail_stack =
                        bytes_hex(emu.read_debug_memory(st.sp, 12), 12);
                    child_crt0_tail_hl_bytes =
                        bytes_hex(emu.read_debug_memory(st.hl, 16), 16);
                    child_crt0_tail_de_bytes =
                        bytes_hex(emu.read_debug_memory(st.de, 16), 16);
                    saw_child_crt0_tail = true;
                }
	                if (!saw_child_main && have_app_map &&
	                    pc == child_base + A.at("_main") + app_symbol_bias) {
	                    child_main_pc = pc;
	                    saw_child_main = true;
                        child_user_pc_trace_len = 0;
	                }
	                if (!saw_child_boot_after_init && have_app_map &&
	                    A.syms.count("_app_bootstrap") != 0 &&
	                    pc == (uint16_t)(child_base + A.at("_app_bootstrap") +
	                                     app_symbol_bias + 0x000c)) {
	                    const auto st = emu.capture_debug_cpu_state();
	                    child_boot_after_init_pc = pc;
	                    child_boot_after_init_de = st.de;
	                    child_boot_after_init_hl = st.hl;
	                    child_boot_after_init_iy = st.iy;
	                    saw_child_boot_after_init = true;
	                }
	                if (!saw_child_pa_init_after_rst &&
	                    pc == (uint16_t)(child_base + 0x0036)) {
	                    const auto st = emu.capture_debug_cpu_state();
	                    child_pa_init_after_rst_pc = pc;
	                    child_pa_init_after_rst_de = st.de;
	                    child_pa_init_after_rst_hl = st.hl;
	                    saw_child_pa_init_after_rst = true;
	                }
	                if (!saw_child_pa_dead && have_app_map &&
	                    A.syms.count("_pa_dead") != 0 &&
	                    pc == (uint16_t)(child_base + A.at("_pa_dead") +
	                                     app_symbol_bias)) {
	                    const auto st = emu.capture_debug_cpu_state();
	                    child_pa_dead_pc = pc;
	                    child_pa_dead_sp = st.sp;
	                    child_pa_dead_stack =
	                        bytes_hex(emu.read_debug_memory(st.sp, 16), 16);
	                    saw_child_pa_dead = true;
	                }
	                if (!saw_child_app_write_cstr && have_app_map &&
	                    A.syms.count("_app_write_cstr") != 0 &&
	                    pc == child_base + A.at("_app_write_cstr") + app_symbol_bias) {
                    const auto st = emu.capture_debug_cpu_state();
                    child_app_write_cstr_pc = pc;
                    child_app_write_cstr_hl = st.hl;
                    saw_child_app_write_cstr = true;
                }
                if (!saw_child_write_console && pc == pc_write_console) {
                    const auto st = emu.capture_debug_cpu_state();
                    child_write_hl = st.hl;
                    child_write_de = st.de;
                    child_write_bytes =
                        bytes_hex(emu.read_debug_memory(st.hl, st.de > 24 ? 24 : st.de),
                                  st.de > 24 ? 24 : st.de);
                    saw_child_write_console = true;
                }
                if (have_app_map && pc_app_read_directory != 0 &&
                    readdir_samples.size() < 32 &&
                    pc == (uint16_t)(child_base + pc_app_read_directory)) {
                    const auto st = emu.capture_debug_cpu_state();
                    pending_readdir_info =
                        rd16(emu.read_debug_memory((uint16_t)(st.sp + 2), 2));
                    pending_readdir_path = st.de;
                    pending_readdir_call_pc = pc;
                    pending_readdir_saw_busy = false;
                    if (st.de != 0)
                        last_ls_path_ptr = st.de;
                }
                if (pending_readdir_info != 0 && readdir_samples.size() < 32) {
                    const auto info =
                        emu.read_debug_memory(pending_readdir_info,
                                              FATDIRINFO_SIZE);
                    if (info.size() >= FATDIRINFO_SIZE) {
                        const uint16_t status =
                            rd16(info, FATDIRINFO_STATUS_OFF);
                        if (status == FAT_EBUSY_VALUE) {
                            pending_readdir_saw_busy = true;
                        } else if (pending_readdir_saw_busy) {
                            readdir_sample sample;
                            sample.tick = emu.get_tick_count();
                            sample.call_pc = pending_readdir_call_pc;
                            sample.info = pending_readdir_info;
                            sample.path = pending_readdir_path;
                            sample.first_cluster = rd16(info, 0);
                            sample.size_lo = rd16(info, 2);
                            sample.size_hi = rd16(info, 4);
                            sample.attr =
                                info.size() > 9 ? info[9] : 0xff;
                            sample.status = status;
                            sample.index = rd16(info, FATDIRINFO_INDEX_OFF);
                            sample.path_text =
                                pending_readdir_path == 0
                                    ? ""
                                    : read_cstr_bounded(
                                          emu, pending_readdir_path, 64);
                            sample.info_bytes = bytes_hex(info, info.size());
                            sample.name_bytes =
                                bytes_hex(info.size() > FATDIRINFO_NAME_OFF
                                              ? std::vector<uint8_t>(
                                                    info.begin() +
                                                        FATDIRINFO_NAME_OFF,
                                                    info.end())
                                              : std::vector<uint8_t>{},
                                          11);
                            readdir_samples.push_back(sample);
                            pending_readdir_info = 0;
                            pending_readdir_saw_busy = false;
                        }
                    }
                }
                if (have_app_map && pc_ls_write_name != 0 &&
                    last_ls_path_ptr != 0 && ls_name_samples.size() < 64 &&
                    pc == (uint16_t)(child_base + pc_ls_write_name)) {
                    const uint16_t dirinfo = (uint16_t)(last_ls_path_ptr + 0x0040);
                    const auto dirinfo_bytes =
                        emu.read_debug_memory(dirinfo, FATDIRINFO_SIZE);
                    const uint16_t index =
                        rd16(dirinfo_bytes, FATDIRINFO_INDEX_OFF);
                    if (index != last_ls_name_index) {
                        const auto st = emu.capture_debug_cpu_state();
                        last_ls_name_index = index;
                    ls_name_sample sample;
                    sample.tick = emu.get_tick_count();
                    sample.pc = pc;
                    sample.path = last_ls_path_ptr;
                        sample.dirinfo = dirinfo;
                    sample.name = (uint16_t)(last_ls_path_ptr + 0x005B);
                        sample.ret = rd16(emu.read_debug_memory(st.sp, 2));
                        sample.sp = st.sp;
                        sample.index = index;
                        sample.first_cluster = rd16(dirinfo_bytes, 0);
                        sample.size_lo = rd16(dirinfo_bytes, 2);
                        sample.size_hi = rd16(dirinfo_bytes, 4);
                        sample.attr =
                            dirinfo_bytes.size() > 9 ? dirinfo_bytes[9] : 0xff;
                    sample.name_bytes =
                        bytes_hex(emu.read_debug_memory(sample.name, 14), 14);
                    sample.after_bytes =
                        bytes_hex(emu.read_debug_memory(sample.name, 64), 64);
                    sample.dirinfo_bytes =
                            bytes_hex(dirinfo_bytes,
                                  FATDIRINFO_SIZE);
                    ls_name_samples.push_back(sample);
                    }
                }
                if (pc == pc_fat_write_call && fat_write_calls.size() < 8) {
                    const auto st = emu.capture_debug_cpu_state();
                    const auto f = emu.read_debug_memory(st.hl, FATFILE_POS_OFF + 4);
                    fat_write_call_sample sample;
                    sample.tick = emu.get_tick_count();
                    sample.pc = pc;
                    sample.file = st.hl;
                    sample.buf = st.de;
                    sample.first_cluster = rd16(f, FATFILE_FIRST_CLUSTER_OFF);
                    sample.size_lo = rd16(f, 2);
                    sample.size_hi = rd16(f, 4);
                    sample.attr =
                        f.size() > 9 ? f[9] : 0xff;
                    sample.status = rd16(f, 10);
                    sample.fs = rd16(f, FATFILE_FS_OFF);
                    sample.pos_lo = rd16(f, FATFILE_POS_OFF);
                    sample.pos_hi = rd16(f, (size_t)FATFILE_POS_OFF + 2);
                    sample.file_bytes = bytes_hex(f, f.size());
                    fat_write_calls.push_back(sample);
                }
                if (!saw_child_path_wrapper && have_app_map &&
                    pc >= (uint16_t)(child_base + 0x0080) &&
                    pc <= (uint16_t)(child_base + 0x00DE)) {
                    const auto st = emu.capture_debug_cpu_state();
                    child_path_wrapper_pc = pc;
                    child_path_wrapper_sp = st.sp;
                    child_path_wrapper_hl = st.hl;
                    child_path_wrapper_de = st.de;
                    child_path_wrapper_bc = st.bc;
                    child_path_wrapper_stack =
                        bytes_hex(emu.read_debug_memory(st.sp, 16), 16);
                    saw_child_path_wrapper = true;
                }
                if (!saw_child_path_precall && have_app_map &&
                    pc == (uint16_t)(child_base + 0x00DB)) {
                    const auto st = emu.capture_debug_cpu_state();
                    child_path_precall_pc = pc;
                    child_path_precall_sp = st.sp;
                    child_path_precall_hl = st.hl;
                    child_path_precall_de = st.de;
                    child_path_precall_bc = st.bc;
                    child_path_precall_stack =
                        bytes_hex(emu.read_debug_memory(st.sp, 16), 16);
                    saw_child_path_precall = true;
                }
                if (!saw_child_path_postcall && have_app_map &&
                    pc == (uint16_t)(child_base + 0x00DE)) {
                    const auto st = emu.capture_debug_cpu_state();
                    child_path_postcall_pc = pc;
                    child_path_postcall_sp = st.sp;
                    child_path_postcall_hl = st.hl;
                    child_path_postcall_de = st.de;
                    child_path_postcall_bc = st.bc;
                    child_path_postcall_stack =
                        bytes_hex(emu.read_debug_memory(st.sp, 16), 16);
                    saw_child_path_postcall = true;
                }
                if (have_app_map &&
                    pc == (uint16_t)(child_base + 0x00DE) &&
                    child_path_postcall_count < child_path_postcall_bcs.size()) {
                    const auto st = emu.capture_debug_cpu_state();
                    child_path_postcall_bcs[child_path_postcall_count] = st.bc;
                    child_path_postcall_des[child_path_postcall_count] = st.de;
                    child_path_postcall_sps[child_path_postcall_count] = st.sp;
                    child_path_postcall_count++;
                }
                if (!saw_child_path_return && have_app_map &&
                    pc == (uint16_t)(child_base + 0x00E1)) {
                    const auto st = emu.capture_debug_cpu_state();
                    child_path_return_pc = pc;
                    child_path_return_sp = st.sp;
                    child_path_return_hl = st.hl;
                    child_path_return_de = st.de;
                    child_path_return_bc = st.bc;
                    child_path_return_stack =
                        bytes_hex(emu.read_debug_memory(st.sp, 16), 16);
                    saw_child_path_return = true;
                }
                if (have_app_map &&
                    pc == (uint16_t)(child_base + 0x00E1) &&
                    child_path_return_count < child_path_return_des.size()) {
                    const auto st = emu.capture_debug_cpu_state();
                    child_path_return_des[child_path_return_count] = st.de;
                    child_path_return_bcs[child_path_return_count] = st.bc;
                    child_path_return_sps[child_path_return_count] = st.sp;
                    child_path_return_events[child_path_return_count] =
                        child_base == 0
                            ? 0
                            : rd16(emu.read_debug_memory(
                                      (uint16_t)(child_base + app_symbol_bias +
                                                 APP_RUNTIME_EVENT_OFF),
                                      2));
                    child_path_return_count++;
                }
                if (!saw_child_fat_create && pc == O.at("_fat_create")) {
                    const auto st = emu.capture_debug_cpu_state();
                    child_fat_create_pc = pc;
                    child_fat_create_ret_pc =
                        rd16(emu.read_debug_memory(st.sp, 2));
                    child_fat_create_caller_ret_pc =
                        rd16(emu.read_debug_memory((uint16_t)(st.sp + 6), 2));
                    child_fat_create_sp = st.sp;
                    child_fat_create_hl = st.hl;
                    child_fat_create_de = st.de;
                    child_fat_create_bc = st.bc;
                    child_fat_create_stack =
                        bytes_hex(emu.read_debug_memory(st.sp, 16), 16);
                    child_fat_create_caller_bytes =
                        bytes_hex(emu.read_debug_memory(
                                      (uint16_t)(child_fat_create_caller_ret_pc - 8),
                                      24),
                                  24);
                    saw_child_fat_create = true;
                }
                if (saw_child_fat_create && !saw_child_fat_create_return &&
                    pc == child_fat_create_ret_pc) {
                    const auto st = emu.capture_debug_cpu_state();
                    child_fat_create_return_pc = pc;
                    child_fat_create_return_sp = st.sp;
                    child_fat_create_return_hl = st.hl;
                    child_fat_create_return_de = st.de;
                    child_fat_create_return_bc = st.bc;
                    child_fat_create_return_ix = st.ix;
                    child_fat_create_return_iy = st.iy;
                    child_fat_create_return_stack =
                        bytes_hex(emu.read_debug_memory(st.sp, 16), 16);
                    saw_child_fat_create_return = true;
                }
                if (saw_child_fat_create &&
                    !saw_child_fat_create_caller &&
                    pc == child_fat_create_caller_ret_pc) {
                    const auto st = emu.capture_debug_cpu_state();
                    child_fat_create_caller_pc = pc;
                    child_fat_create_caller_sp = st.sp;
                    child_fat_create_caller_hl = st.hl;
                    child_fat_create_caller_de = st.de;
                    child_fat_create_caller_bc = st.bc;
                    child_fat_create_caller_stack =
                        bytes_hex(emu.read_debug_memory(st.sp, 16), 16);
                    saw_child_fat_create_caller = true;
                }
                if (!saw_child_svc_query && pc == pc_svc_query) {
                    const auto st = emu.capture_debug_cpu_state();
                    child_svc_query_hl = st.hl;
                    child_svc_query_name = read_cstr_bounded(emu, st.hl, 24);
                    saw_child_svc_query = true;
                }
                if (pc == pc_svc_query &&
                    child_svc_query_count < child_svc_query_pcs.size()) {
                    const auto st = emu.capture_debug_cpu_state();
                    child_svc_query_pcs[child_svc_query_count] = pc;
                    child_svc_query_hls[child_svc_query_count] = st.hl;
                    child_svc_query_names[child_svc_query_count] =
                        read_cstr_bounded(emu, st.hl, 24);
                    child_svc_query_count++;
                }
                if (pc >= pc_svc_query && pc < (uint16_t)(pc_svc_query + 0x30) &&
                    child_svc_pc_trace_len < child_svc_pc_trace.size() &&
                    (child_svc_pc_trace_len == 0 ||
                     child_svc_pc_trace[child_svc_pc_trace_len - 1] != pc)) {
                    child_svc_pc_trace[child_svc_pc_trace_len++] = pc;
                }
                if (!saw_child_svc_found &&
                    pc == (uint16_t)(pc_svc_query + 0x1f)) {
                    const auto st = emu.capture_debug_cpu_state();
                    child_svc_found_pc = pc;
                    child_svc_found_hl = st.hl;
                    saw_child_svc_found = true;
                }
            }
            if (target_main_thread != 0 && child_wait_sp == 0) {
                const auto thread_bytes =
                    emu.read_debug_memory(target_main_thread, 32);

                if (thread_bytes.size() >= 20 && thread_bytes[19] == 2) {
                    child_wait_sp = rd16(thread_bytes, 4);
                    child_wait_ptr = rd16(thread_bytes, 16);
                    child_wait_num = thread_bytes[18];
                    child_wait_stack =
                        bytes_hex(emu.read_debug_memory(child_wait_sp, 16), 16);
                    if (child_wait_ptr != 0) {
                        const auto wait_bytes =
                            emu.read_debug_memory(child_wait_ptr, 8);
                        child_wait_bytes = bytes_hex(wait_bytes, 8);
                        if (wait_bytes.size() >= 2) {
                            child_wait_event = rd16(wait_bytes, 0);
                            if (child_wait_event != 0) {
                                const auto evt_bytes =
                                    emu.read_debug_memory(child_wait_event, 5);
                                if (evt_bytes.size() >= 5)
                                    child_wait_event_state = evt_bytes[4];
                            }
                        }
                    }
                }
            }
            if ((child_stack_next_block != 0) &&
                !child_stack_next_hdr.empty() &&
                (child_stack_next_hdr_changes < 8)) {
                const auto next_hdr_now =
                    emu.read_debug_memory(child_stack_next_block, child_stack_next_hdr.size());
                if ((next_hdr_now.size() == child_stack_next_hdr.size()) &&
                    (next_hdr_now != child_stack_next_hdr)) {
                    const auto st = emu.capture_debug_cpu_state();
                    child_stack_next_hdr_changes++;
                    std::printf(
                        "trace: child_next_hdr_change #%u pc=%04X sp=%04X cur=%04X block=%04X next=%04X old=%s new=%s\n",
                        child_stack_next_hdr_changes,
                        pc,
                        st.sp,
                        current,
                        child_stack_block,
                        child_stack_next_block,
                        bytes_hex(child_stack_next_hdr, 16).c_str(),
                        bytes_hex(next_hdr_now, 16).c_str());
                    child_stack_next_hdr = next_hdr_now;
                }
            }
        }
        if ((n & 0x3FFu) != 0)
            continue;
        const std::string term = emu.dump_terminal_text();
        const std::string raw = emu.dump_raw_serial_text();
        if ((term != initial_term || raw.find(command) != std::string::npos) &&
            prompt_returned(term, raw)) {
            returned = true;
            if (break_on_prompt)
                break;
        }
    }

    std::printf("returned=%d pc=%04X tick=%llu\n",
                returned ? 1 : 0,
                emu.get_current_pc(),
                (unsigned long long)emu.get_tick_count());
    std::printf("pc.sym=%s\n", O.nearest(emu.get_current_pc()).c_str());
    {
        const auto cpu = emu.capture_debug_cpu_state();
        std::printf("cpu: af=%04X bc=%04X de=%04X hl=%04X ix=%04X iy=%04X sp=%04X iff1=%u halted=%u\n",
                    cpu.af, cpu.bc, cpu.de, cpu.hl, cpu.ix, cpu.iy, cpu.sp,
                    cpu.iff1 ? 1U : 0U,
                    cpu.halted ? 1U : 0U);
        std::printf("cpu.stack: %s\n",
                    bytes_hex(emu.read_debug_memory(cpu.sp, 24), 24).c_str());
    }
    std::printf("trace: run_command_hits=%llu first_run=%llu process_wait_hits=%llu first_wait=%llu\n",
                (unsigned long long)hits_run_command,
                (unsigned long long)first_run_command_tick,
                (unsigned long long)hits_process_wait,
                (unsigned long long)first_process_wait_tick);
    if (first_svc_register_tick != 0) {
        std::printf("trace: svc_register_hits=%llu first_reg=%llu hl=%04X de=%04X name='%s'\n",
                    (unsigned long long)hits_svc_register,
                    (unsigned long long)first_svc_register_tick,
                    first_svc_register_hl,
                    first_svc_register_de,
                    first_svc_register_name.c_str());
        std::printf("trace: svc_register_name_bytes=%s\n",
                    first_svc_register_name_bytes.c_str());
        std::printf("trace: svc_register_table_bytes=%s\n",
                    first_svc_register_table_bytes.c_str());
    }
    if (first_process_wait_tick != 0) {
        std::printf("trace: first_wait_current=%04X running=%04X current_next=%04X\n",
                    first_wait_current, first_wait_running, first_wait_current_next);
        std::printf("trace: pwait_hl=%04X pwait_de=%04X\n",
                    rd16(emu.read_debug_memory(pwait_hl_addr, 2)),
                    rd16(emu.read_debug_memory(pwait_de_addr, 2)));
        std::printf("trace: pwait_target=%04X\n",
                    rd16(emu.read_debug_memory(pwait_target_addr, 2)));
        if (wait_trace_len != 0) {
            std::string trace;
            char buf[32];

            for (size_t i = 0; i < wait_trace_len; ++i) {
                if (!trace.empty())
                    trace += " ";
                std::snprintf(buf, sizeof(buf), "%04X/%04X",
                              wait_current_trace[i], wait_pc_trace[i]);
                trace += buf;
            }
            std::printf("trace: wait_trace=%s\n", trace.c_str());
        }
    }
    std::printf("trace: child_current_hits=%llu first_child=%llu\n",
                (unsigned long long)child_current_hits,
                (unsigned long long)first_child_current_tick);
    if (target_main_thread != 0) {
        std::printf("trace: target_process=%04X target_main=%04X name='%s'\n",
                    target_process, target_main_thread, target_name.c_str());
    }
    if (captured_child_process_state) {
        std::printf(
            "trace: child_process ptr=%04X current.process=%04X cmd=%04X env=%04X cmdtext='%s' envbytes=%s libc=%04X libc_name_bytes=%s\n",
            target_process,
            child_process_ptr,
            child_process_cmd,
            child_process_env,
            child_process_cmd_text.c_str(),
            child_process_env_bytes.c_str(),
            child_libc_service,
            child_libc_name_bytes.c_str());
    }
    std::printf("trace: first_child_pc=%04X\n", first_child_pc);
    if (child_pc_trace_len != 0) {
        std::string trace;
        char buf[8];

        for (size_t i = 0; i < child_pc_trace_len; ++i) {
            if (!trace.empty())
                trace += " ";
            std::snprintf(buf, sizeof(buf), "%04X", child_pc_trace[i]);
            trace += buf;
        }
        std::printf("trace: child_pc_trace=%s\n", trace.c_str());
    }
    if (current_trace_len != 0) {
        std::string trace;
        char buf[96];

        for (size_t i = 0; i < current_trace_len; ++i) {
            if (!trace.empty())
                trace += " | ";
            std::snprintf(buf, sizeof(buf), "%04X@%04X#%llu",
                          current_trace_threads[i],
                          current_trace_pcs[i],
                          (unsigned long long)current_trace_ticks[i]);
            trace += buf;
        }
        std::printf("trace: current_trace=%s\n", trace.c_str());
    }
    if (have_app_map) {
        std::printf("trace: child_app_range base=%04X top=%04X bias=%04X\n",
                    child_app_base, child_app_top, app_symbol_bias);
        std::printf("trace: child_user_hits=%llu first_user=%llu first_user_pc=%04X\n",
                    (unsigned long long)child_user_hits,
                    (unsigned long long)first_child_user_tick,
                    first_child_user_pc);
        if (child_user_pc_trace_len != 0) {
            std::string trace;
            char buf[8];

            for (size_t i = 0; i < child_user_pc_trace_len; ++i) {
                if (!trace.empty())
                    trace += " ";
                std::snprintf(buf, sizeof(buf), "%04X", child_user_pc_trace[i]);
                trace += buf;
            }
            std::printf("trace: child_user_pc_trace=%s\n", trace.c_str());
        }
    }
    if (child_wait_sp != 0) {
        std::printf("trace: child_wait sp=%04X wait=%04X num=%u event=%04X state=%02X waitmem=%s stack=%s\n",
                    child_wait_sp,
                    child_wait_ptr,
                    (unsigned)child_wait_num,
                    child_wait_event,
                    child_wait_event_state,
                    child_wait_bytes.c_str(),
                    child_wait_stack.c_str());
    }
    if (saw_child_call_hl) {
        std::printf("trace: child_call_hl pc=%04X hl=%04X\n",
                    child_call_hl_pc, child_call_hl_hl);
    }
    if (saw_child_call_iy) {
        std::printf("trace: child_call_iy pc=%04X iy=%04X\n",
                    child_call_iy_pc, child_call_iy_iy);
    }
    if (saw_child_main) {
        std::printf("trace: child_main pc=%04X\n", child_main_pc);
    }
    if (saw_child_app_write_cstr) {
        std::printf("trace: child_app_write_cstr pc=%04X hl=%04X\n",
                    child_app_write_cstr_pc, child_app_write_cstr_hl);
    }
    if (saw_child_write_console) {
        std::printf("trace: child_write_console hl=%04X de=%04X bytes=%s\n",
                    child_write_hl, child_write_de, child_write_bytes.c_str());
    }
    if (!fat_write_calls.empty()) {
        std::printf("trace: fat_write_calls_begin count=%zu\n",
                    fat_write_calls.size());
        for (size_t i = 0; i < fat_write_calls.size(); ++i) {
            const auto &w = fat_write_calls[i];
            std::printf(
                "trace: fat_write_call[%zu] tick=%llu pc=%04X file=%04X buf=%04X first=%04X size=%04X:%04X attr=%02X status=%04X fs=%04X pos=%04X:%04X bytes=%s\n",
                i,
                (unsigned long long)w.tick,
                w.pc,
                w.file,
                w.buf,
                w.first_cluster,
                w.size_hi,
                w.size_lo,
                w.attr,
                w.status,
                w.fs,
                w.pos_hi,
                w.pos_lo,
                w.file_bytes.c_str());
        }
        std::printf("trace: fat_write_calls_end\n");
    }
    if (!readdir_samples.empty()) {
        std::printf("trace: readdir_samples_begin count=%zu\n",
                    readdir_samples.size());
        for (size_t i = 0; i < readdir_samples.size(); ++i) {
            const auto &r = readdir_samples[i];
            std::printf(
                "trace: readdir_sample[%zu] tick=%llu call=%04X info=%04X path=%04X path_text='%s' first=%04X size=%04X:%04X attr=%02X status=%04X index=%04X name=%s bytes=%s\n",
                i,
                (unsigned long long)r.tick,
                r.call_pc,
                r.info,
                r.path,
                r.path_text.c_str(),
                r.first_cluster,
                r.size_hi,
                r.size_lo,
                r.attr,
                r.status,
                r.index,
                r.name_bytes.c_str(),
                r.info_bytes.c_str());
        }
        std::printf("trace: readdir_samples_end\n");
    }
    if (!ls_name_samples.empty()) {
        std::printf("trace: ls_name_samples_begin count=%zu\n",
                    ls_name_samples.size());
        for (size_t i = 0; i < ls_name_samples.size(); ++i) {
            const auto &s = ls_name_samples[i];
            std::printf(
                "trace: ls_name_sample[%zu] tick=%llu pc=%04X ret=%04X sp=%04X path=%04X dirinfo=%04X index=%04X first=%04X size=%04X:%04X attr=%02X name=%04X name_bytes=%s after=%s dirinfo=%s\n",
                i,
                (unsigned long long)s.tick,
                s.pc,
                s.ret,
                s.sp,
                s.path,
                s.dirinfo,
                s.index,
                s.first_cluster,
                s.size_hi,
                s.size_lo,
                s.attr,
                s.name,
                s.name_bytes.c_str(),
                s.after_bytes.c_str(),
                s.dirinfo_bytes.c_str());
        }
        std::printf("trace: ls_name_samples_end\n");
    }
    if (!console_writes.empty()) {
        std::printf("trace: console_writes_begin count=%zu\n",
                    console_writes.size());
        for (size_t i = 0; i < console_writes.size(); ++i) {
            const auto &w = console_writes[i];
            std::printf("trace: console_write[%03zu] tick=%llu thread=%04X process=%04X name='%s' ret=%04X sp=%04X hl=%04X de=%04X bytes=%s stack=%s\n",
                        i,
                        (unsigned long long)w.tick,
                        w.thread,
                        w.process,
                        w.pname.c_str(),
                        w.ret,
                        w.sp,
                        w.hl,
                        w.de,
                        w.bytes.c_str(),
                        w.stack.c_str());
        }
        std::printf("trace: console_writes_end\n");
    }
    if (saw_child_path_wrapper) {
        std::printf("trace: child_path_wrapper pc=%04X sp=%04X hl=%04X de=%04X bc=%04X stack=%s\n",
                    child_path_wrapper_pc,
                    child_path_wrapper_sp,
                    child_path_wrapper_hl,
                    child_path_wrapper_de,
                    child_path_wrapper_bc,
                    child_path_wrapper_stack.c_str());
    }
    if (saw_child_path_precall) {
        std::printf("trace: child_path_precall pc=%04X sp=%04X hl=%04X de=%04X bc=%04X stack=%s\n",
                    child_path_precall_pc,
                    child_path_precall_sp,
                    child_path_precall_hl,
                    child_path_precall_de,
                    child_path_precall_bc,
                    child_path_precall_stack.c_str());
    }
    if (saw_child_path_postcall) {
        std::printf("trace: child_path_postcall pc=%04X sp=%04X hl=%04X de=%04X bc=%04X stack=%s\n",
                    child_path_postcall_pc,
                    child_path_postcall_sp,
                    child_path_postcall_hl,
                    child_path_postcall_de,
                    child_path_postcall_bc,
                    child_path_postcall_stack.c_str());
    }
    if (saw_child_path_return) {
        std::printf("trace: child_path_return pc=%04X sp=%04X hl=%04X de=%04X bc=%04X stack=%s\n",
                    child_path_return_pc,
                    child_path_return_sp,
                    child_path_return_hl,
                    child_path_return_de,
                    child_path_return_bc,
                    child_path_return_stack.c_str());
    }
    if (child_path_postcall_count != 0) {
        std::string trace;
        char buf[48];

        for (size_t i = 0; i < child_path_postcall_count; ++i) {
            if (!trace.empty())
                trace += " ";
            std::snprintf(buf, sizeof(buf), "%zu:%04X/%04X/%04X",
                          i,
                          child_path_postcall_bcs[i],
                          child_path_postcall_des[i],
                          child_path_postcall_sps[i]);
            trace += buf;
        }
        std::printf("trace: child_path_postcalls=%s\n", trace.c_str());
    }
    if (child_path_return_count != 0) {
        std::string trace;
        char buf[48];

        for (size_t i = 0; i < child_path_return_count; ++i) {
            if (!trace.empty())
                trace += " ";
            std::snprintf(buf, sizeof(buf), "%zu:%04X/%04X/%04X",
                          i,
                          child_path_return_bcs[i],
                          child_path_return_des[i],
                          child_path_return_sps[i]);
            if (child_path_return_events[i] != 0) {
                char evtbuf[24];

                std::snprintf(evtbuf, sizeof(evtbuf), "@%04X",
                              child_path_return_events[i]);
                trace += evtbuf;
            }
            trace += buf;
        }
        std::printf("trace: child_path_returns=%s\n", trace.c_str());
    }
    if (saw_child_fat_create) {
        std::printf("trace: child_fat_create pc=%04X ret=%04X caller_ret=%04X sp=%04X hl=%04X de=%04X bc=%04X stack=%s\n",
                    child_fat_create_pc,
                    child_fat_create_ret_pc,
                    child_fat_create_caller_ret_pc,
                    child_fat_create_sp,
                    child_fat_create_hl,
                    child_fat_create_de,
                    child_fat_create_bc,
                    child_fat_create_stack.c_str());
        std::printf("trace: child_fat_create_caller_bytes=%s\n",
                    child_fat_create_caller_bytes.c_str());
    }
    if (saw_child_fat_create_return) {
        std::printf("trace: child_fat_create_return pc=%04X sp=%04X hl=%04X de=%04X bc=%04X ix=%04X iy=%04X stack=%s\n",
                    child_fat_create_return_pc,
                    child_fat_create_return_sp,
                    child_fat_create_return_hl,
                    child_fat_create_return_de,
                    child_fat_create_return_bc,
                    child_fat_create_return_ix,
                    child_fat_create_return_iy,
                    child_fat_create_return_stack.c_str());
    }
    if (saw_child_fat_create_caller) {
        std::printf("trace: child_fat_create_caller pc=%04X sp=%04X hl=%04X de=%04X bc=%04X stack=%s\n",
                    child_fat_create_caller_pc,
                    child_fat_create_caller_sp,
                    child_fat_create_caller_hl,
                    child_fat_create_caller_de,
                    child_fat_create_caller_bc,
                    child_fat_create_caller_stack.c_str());
    }
    if (fat_worker_loop_hits != 0 || fat_worker_create_hits != 0) {
        std::printf("trace: fat_worker loop_hits=%llu create_hits=%llu first_create_pc=%04X sp=%04X req=%04X reqbytes=%s stack=%s\n",
                    (unsigned long long)fat_worker_loop_hits,
                    (unsigned long long)fat_worker_create_hits,
                    fat_worker_create_pc,
                    fat_worker_create_sp,
                    fat_worker_create_req,
                    fat_worker_create_req_bytes.c_str(),
                    fat_worker_create_stack.c_str());
    }
    if (fat_finish_hits != 0) {
        std::printf("trace: fat_finish hits=%llu pc=%04X sp=%04X status=%04X event=%04X event_state=%02X dirent=%04X stack=%s\n",
                    (unsigned long long)fat_finish_hits,
                    fat_finish_pc,
                    fat_finish_sp,
                    fat_finish_status,
                    fat_finish_event,
                    fat_finish_event_state,
                    fat_finish_dirent,
                    fat_finish_stack.c_str());
    }
    if (!fat_worker_helper_trace.empty()) {
        std::printf("trace: fat_worker_helpers_begin\n");
        for (const std::string &line : fat_worker_helper_trace)
            std::printf("trace: %s\n", line.c_str());
        std::printf("trace: fat_worker_helpers_end\n");
    }
    if (evt_set_count != 0) {
        std::string trace;
        char buf[64];

        for (size_t i = 0; i < evt_set_count; ++i) {
            if (!trace.empty())
                trace += " ";
            std::snprintf(buf, sizeof(buf), "%04X/%02X/%02X#%llu",
                          evt_set_events[i],
                          (unsigned)evt_set_args[i],
                          (unsigned)evt_set_states[i],
                          (unsigned long long)evt_set_ticks[i]);
            trace += buf;
        }
        std::printf("trace: evt_set=%s\n", trace.c_str());
    }
    if (evt_destroy_count != 0) {
        std::string trace;
        char buf[48];

        for (size_t i = 0; i < evt_destroy_count; ++i) {
            if (!trace.empty())
                trace += " ";
            std::snprintf(buf, sizeof(buf), "%04X#%llu",
                          evt_destroy_events[i],
                          (unsigned long long)evt_destroy_ticks[i]);
            trace += buf;
        }
        std::printf("trace: evt_destroy=%s\n", trace.c_str());
    }
    if (target_evt_set_count != 0) {
        std::string trace;
        char buf[48];

        for (size_t i = 0; i < target_evt_set_count; ++i) {
            if (!trace.empty())
                trace += " ";
            std::snprintf(buf, sizeof(buf), "%02X/%02X#%llu",
                          (unsigned)target_evt_set_args[i],
                          (unsigned)target_evt_set_states[i],
                          (unsigned long long)target_evt_set_ticks[i]);
            trace += buf;
        }
        std::printf("trace: evt_set_target=%04X %s\n",
                    child_wait_event, trace.c_str());
    }
    if (saw_child_svc_query) {
        std::printf("trace: child_svc_query hl=%04X name='%s'\n",
                    child_svc_query_hl, child_svc_query_name.c_str());
    }
    if (child_svc_query_count != 0) {
        std::string trace;
        char buf[64];

        for (size_t i = 0; i < child_svc_query_count; ++i) {
            if (!trace.empty())
                trace += " ";
            std::snprintf(buf, sizeof(buf), "%04X:%04X:%s",
                          child_svc_query_pcs[i],
                          child_svc_query_hls[i],
                          child_svc_query_names[i].c_str());
            trace += buf;
        }
        std::printf("trace: child_svc_queries=%s\n", trace.c_str());
    }
    if (child_svc_pc_trace_len != 0) {
        std::string trace;
        char buf[8];

        for (size_t i = 0; i < child_svc_pc_trace_len; ++i) {
            if (!trace.empty())
                trace += " ";
            std::snprintf(buf, sizeof(buf), "%04X", child_svc_pc_trace[i]);
            trace += buf;
        }
        std::printf("trace: child_svc_pc_trace=%s\n", trace.c_str());
    }
    if (saw_child_svc_found) {
        std::printf("trace: child_svc_found pc=%04X hl=%04X\n",
                    child_svc_found_pc,
                    child_svc_found_hl);
    }
    if (saw_child_crt0_tail) {
        std::printf("trace: child_crt0_tail pc=%04X hl=%04X de=%04X bc=%04X sp=%04X stack=%s hlmem=%s demem=%s\n",
                    child_crt0_tail_pc,
                    child_crt0_tail_hl,
                    child_crt0_tail_de,
                    child_crt0_tail_bc,
                    child_crt0_tail_sp,
                    child_crt0_tail_stack.c_str(),
                    child_crt0_tail_hl_bytes.c_str(),
                    child_crt0_tail_de_bytes.c_str());
    }
    if (saw_child_boot_after_init) {
        std::printf("trace: child_boot_after_init pc=%04X de=%04X hl=%04X iy=%04X\n",
                    child_boot_after_init_pc,
                    child_boot_after_init_de,
                    child_boot_after_init_hl,
                    child_boot_after_init_iy);
    }
    if (saw_child_pa_init_after_rst) {
        std::printf("trace: child_pa_init_after_rst pc=%04X de=%04X hl=%04X\n",
                    child_pa_init_after_rst_pc,
                    child_pa_init_after_rst_de,
                    child_pa_init_after_rst_hl);
    }
    if (saw_child_pa_dead) {
        std::printf("trace: child_pa_dead pc=%04X sp=%04X stack=%s\n",
                    child_pa_dead_pc,
                    child_pa_dead_sp,
                    child_pa_dead_stack.c_str());
    }
    std::printf("threads: current=%04X running=%04X waiting=%04X terminated=%04X\n",
                rd16(emu.read_debug_memory(K.at("_thread_current"), 2)),
                rd16(emu.read_debug_memory(K.at("_thread_first_running"), 2)),
                rd16(emu.read_debug_memory(K.at("_thread_first_waiting"), 2)),
                rd16(emu.read_debug_memory(K.at("_thread_first_terminated"), 2)));
    std::printf("thread.create: hits=%llu last_entry=%04X last_stack=%04X last_bank=%04X last_data=%04X fail0=%llu fail1=%llu\n",
                (unsigned long long)thread_create_hits,
                last_thread_create_hl,
                last_thread_create_de,
                last_thread_create_bank,
                last_thread_create_data,
                (unsigned long long)tc_fail0_hits,
                (unsigned long long)tc_fail1_hits);
    std::printf("processes: first=%04X perror=%02X pstage=%02X presult=%04X\n",
                rd16(emu.read_debug_memory(O.at("_process_first"), 2)),
                emu.read_debug_memory(O.at("_process_last_error"), 1).empty()
                    ? 0xFF
                    : emu.read_debug_memory(O.at("_process_last_error"), 1)[0],
                emu.read_debug_memory(O.at("_process_last_stage"), 1).empty()
                    ? 0xFF
                    : emu.read_debug_memory(O.at("_process_last_stage"), 1)[0],
                rd16(emu.read_debug_memory(O.at("_process_last_result"), 2)));
    {
        const uint16_t svc = rd16(emu.read_debug_memory(syscall_service_addr, 2));
        const uint16_t fntable =
            svc == 0 ? 0 : rd16(emu.read_debug_memory((uint16_t)(svc + 20), 2));
        std::printf("syscall.service: svc=%04X table=%04X get_sys=%04X write=%04X query=%04X exit=%04X\n",
                    svc,
                    fntable,
                    fntable == 0 ? 0 : rd16(emu.read_debug_memory((uint16_t)(fntable + 0), 2)),
                    fntable == 0 ? 0 : rd16(emu.read_debug_memory((uint16_t)(fntable + 6), 2)),
                    fntable == 0 ? 0 : rd16(emu.read_debug_memory((uint16_t)(fntable + 42), 2)),
                    fntable == 0 ? 0 : rd16(emu.read_debug_memory((uint16_t)(fntable + 70), 2)));
        std::printf("syscall.fs: mount_dev=%04X mount=%04X lookup=%04X open=%04X create=%04X read=%04X write=%04X readdir=%04X\n",
                    fntable == 0 ? 0 : rd16(emu.read_debug_memory((uint16_t)(fntable + 72), 2)),
                    fntable == 0 ? 0 : rd16(emu.read_debug_memory((uint16_t)(fntable + 74), 2)),
                    fntable == 0 ? 0 : rd16(emu.read_debug_memory((uint16_t)(fntable + 76), 2)),
                    fntable == 0 ? 0 : rd16(emu.read_debug_memory((uint16_t)(fntable + 78), 2)),
                    fntable == 0 ? 0 : rd16(emu.read_debug_memory((uint16_t)(fntable + 80), 2)),
                    fntable == 0 ? 0 : rd16(emu.read_debug_memory((uint16_t)(fntable + 82), 2)),
                    fntable == 0 ? 0 : rd16(emu.read_debug_memory((uint16_t)(fntable + 84), 2)),
                    fntable == 0 ? 0 : rd16(emu.read_debug_memory((uint16_t)(fntable + 86), 2)));
        {
            const uint16_t fat_req_head =
                fat_req_head_addr == 0
                    ? 0
                    : rd16(emu.read_debug_memory(fat_req_head_addr, 2));
            const uint16_t fat_req_tail =
                fat_req_tail_addr == 0
                    ? 0
                    : rd16(emu.read_debug_memory(fat_req_tail_addr, 2));
            const uint16_t fat_queue_event =
                fat_queue_event_addr == 0
                    ? 0
                    : rd16(emu.read_debug_memory(fat_queue_event_addr, 2));
            const uint16_t fat_io_event =
                fat_io_event_addr == 0
                    ? 0
                    : rd16(emu.read_debug_memory(fat_io_event_addr, 2));
            const uint16_t fat_worker_thread =
                fat_worker_thread_addr == 0
                    ? 0
                    : rd16(emu.read_debug_memory(fat_worker_thread_addr, 2));
            const auto fat_init_state_bytes =
                fat_init_state_addr == 0
                    ? std::vector<uint8_t>()
                    : emu.read_debug_memory(fat_init_state_addr, 1);
            const uint8_t fat_init_state =
                fat_init_state_bytes.empty() ? 0 : fat_init_state_bytes[0];
            const uint16_t fat_wait_evt =
                fat_wait_evt_addr == 0
                    ? 0
                    : rd16(emu.read_debug_memory(fat_wait_evt_addr, 2));
            const auto fat_queue_evt_bytes =
                fat_queue_event == 0
                    ? std::vector<uint8_t>()
                    : emu.read_debug_memory(fat_queue_event, 5);
            const auto fat_io_evt_bytes =
                fat_io_event == 0
                    ? std::vector<uint8_t>()
                    : emu.read_debug_memory(fat_io_event, 5);

            std::printf("fat.state: req_head=%04X req_tail=%04X queue_evt=%04X io_evt=%04X worker=%04X init=%02X wait_evt=%04X\n",
                        fat_req_head,
                        fat_req_tail,
                        fat_queue_event,
                        fat_io_event,
                        fat_worker_thread,
                        (unsigned)fat_init_state,
                        fat_wait_evt);
            std::printf("fat.event.bytes: queue=%s io=%s\n",
                        bytes_hex(fat_queue_evt_bytes, 5).c_str(),
                        bytes_hex(fat_io_evt_bytes, 5).c_str());
            if (fat_req_head != 0) {
                std::printf("fat.req_head.bytes: %s\n",
                            bytes_hex(emu.read_debug_memory(fat_req_head, 18), 18).c_str());
            }
            if (fat_req_tail != 0 && fat_req_tail != fat_req_head) {
                std::printf("fat.req_tail.bytes: %s\n",
                            bytes_hex(emu.read_debug_memory(fat_req_tail, 18), 18).c_str());
            }
        }
        const uint16_t svc_head = rd16(emu.read_debug_memory(O.at("__svc_first"), 2));
        const uint16_t svc_next =
            svc_head == 0 ? 0 : rd16(emu.read_debug_memory(svc_head, 2));
        const uint16_t svc_table =
            svc_head == 0 ? 0 : rd16(emu.read_debug_memory((uint16_t)(svc_head + 20), 2));
        const uint16_t svc2_table =
            svc_next == 0 ? 0 : rd16(emu.read_debug_memory((uint16_t)(svc_next + 20), 2));
        const uint16_t svc_next2 =
            svc_next == 0 ? 0 : rd16(emu.read_debug_memory(svc_next, 2));
        const uint16_t svc3_table =
            svc_next2 == 0 ? 0 : rd16(emu.read_debug_memory((uint16_t)(svc_next2 + 20), 2));
        std::printf("svc.list.raw: head=%04X next=%04X next2=%04X table=%04X next_table=%04X next2_table=%04X\n",
                    svc_head, svc_next, svc_next2, svc_table, svc2_table, svc3_table);
        std::printf("svc.list.names: head='%s' next='%s' next2='%s'\n",
                    svc_head == 0 ? "" : read_cstr_bounded(emu, (uint16_t)(svc_head + 4), 16).c_str(),
                    svc_next == 0 ? "" : read_cstr_bounded(emu, (uint16_t)(svc_next + 4), 16).c_str(),
                    svc_next2 == 0 ? "" : read_cstr_bounded(emu, (uint16_t)(svc_next2 + 4), 16).c_str());
    }
    {
        const uint16_t current = rd16(emu.read_debug_memory(K.at("_thread_current"), 2));
        const uint16_t waiting = rd16(emu.read_debug_memory(K.at("_thread_first_waiting"), 2));
        const uint16_t process0 = rd16(emu.read_debug_memory(O.at("_process_first"), 2));
        const uint16_t process1 =
            process0 == 0 ? 0 : rd16(emu.read_debug_memory(process0, 2));
        if (current != 0) {
            const auto t = emu.read_debug_memory(current, 25);
            std::printf("current.thread: next=%04X wait=%04X num=%u state=%u process=%04X bank=%u\n",
                        rd16(t, 0), rd16(t, 16),
                        t.size() > 18 ? (unsigned)t[18] : 0U,
                        t.size() > 19 ? (unsigned)t[19] : 0U,
                        rd16(t, 22),
                        t.size() > 24 ? (unsigned)t[24] : 0U);
            if (t.size() >= 9) {
                const uint16_t shell_base = (uint16_t)(rd16(t, 7) - S.at("shell_entry"));
                const uint16_t shell_run_result =
                    rd16(emu.read_debug_memory((uint16_t)(shell_base + S.at("shell_registered_service$")), 2));
                const uint16_t shell_tmp_ptr =
                    rd16(emu.read_debug_memory((uint16_t)(shell_base + S.at("shell_tmp_ptr$")), 2));
                std::printf("shell.debug: base=%04X run_result=%04X tmp_ptr=%04X\n",
                            shell_base, shell_run_result, shell_tmp_ptr);
            }
        }
        if (waiting != 0) {
            const auto t = emu.read_debug_memory(waiting, 25);
            std::printf("waiting.thread: next=%04X wait=%04X num=%u state=%u process=%04X bank=%u\n",
                        rd16(t, 0), rd16(t, 16),
                        t.size() > 18 ? (unsigned)t[18] : 0U,
                        t.size() > 19 ? (unsigned)t[19] : 0U,
                        rd16(t, 22),
                        t.size() > 24 ? (unsigned)t[24] : 0U);
        }
        if (process0 != 0) {
            const auto p = emu.read_debug_memory(process0, 19);
            const uint16_t main0 = rd16(p, 13);
            std::printf("process[0]: next=%04X name='%s' main=%04X cmd=%04X env=%04X\n",
                        rd16(p, 0),
                        read_cstr_bounded(emu, (uint16_t)(process0 + 5), 8).c_str(),
                        main0, rd16(p, 15), rd16(p, 17));
            if (main0 != 0) {
                const auto t = emu.read_debug_memory(main0, 25);
                std::printf("process[0].main: next=%04X sp=%04X wait=%04X num=%u state=%u process=%04X bank=%u entry=%04X\n",
                            rd16(t, 0), rd16(t, 4), rd16(t, 16),
                            t.size() > 18 ? (unsigned)t[18] : 0U,
                            t.size() > 19 ? (unsigned)t[19] : 0U,
                            rd16(t, 22),
                            t.size() > 24 ? (unsigned)t[24] : 0U,
                            rd16(t, 7));
                std::printf("process[0].main.stack: %s\n",
                            bytes_hex(emu.read_debug_memory(rd16(t, 4), 16), 16)
                                .c_str());
                if (read_cstr_bounded(emu, (uint16_t)(process0 + 5), 8) == "ls") {
                    const uint16_t entry = rd16(t, 7);
                    const uint16_t base = (uint16_t)(entry - 0x0005);
                    const uint16_t data_base = (uint16_t)(base + 0x11C6);
                    const uint16_t ls_data = (uint16_t)(data_base + 0x0128);
                    const uint16_t ls_dirinfo = (uint16_t)(ls_data + 0x0040);
                    const uint16_t ls_dirinfo_name = (uint16_t)(ls_dirinfo + 0x000E);
                    const uint16_t ls_name = (uint16_t)(ls_data + 0x005B);
                    std::printf("process[0].ls.data: base=%04X data=%04X ls_data=%04X\n",
                                base, data_base, ls_data);
                    std::printf("process[0].ls.path: %s\n",
                                bytes_hex(emu.read_debug_memory(ls_data, 64), 64).c_str());
                    std::printf("process[0].ls.dirinfo: %s\n",
                                bytes_hex(emu.read_debug_memory(ls_dirinfo, 25), 25).c_str());
                    std::printf("process[0].ls.dirinfo.name: %s\n",
                                bytes_hex(emu.read_debug_memory(ls_dirinfo_name, 11), 11).c_str());
                    std::printf("process[0].ls.name: %s\n",
                                bytes_hex(emu.read_debug_memory(ls_name, 14), 14).c_str());
                }
                if (have_app_map && rd16(t, 7) >= A.at("_app_crt0_entry")) {
                    const uint16_t base =
                        (uint16_t)(rd16(t, 7) - A.at("_app_crt0_entry"));
                    const uint16_t entry = rd16(t, 7);
                    std::printf("process[0].main.map: base=%04X entry.sym=%s first_user.sym=%s\n",
                                base,
                                A.nearest(A.at("_app_crt0_entry")).c_str(),
                                first_child_user_pc == 0
                                    ? "?"
                                    : A.nearest((uint16_t)(first_child_user_pc - base)).c_str());
                    std::printf("process[0].main.entry.bytes: %s\n",
                                bytes_hex(emu.read_debug_memory(entry, 16)).c_str());
                    std::printf("process[0].main.crt0.tail.bytes: %s\n",
                                bytes_hex(emu.read_debug_memory((uint16_t)(entry + 0x20), 16), 16).c_str());
                    {
                        const uint16_t app_data_base =
                            rd16(emu.read_debug_memory((uint16_t)(entry + 4), 2));
                        std::printf("process[0].main.data: base=%04X words=%04X %04X %04X %04X\n",
                                    app_data_base,
                                    rd16(emu.read_debug_memory(app_data_base, 2)),
                                    rd16(emu.read_debug_memory((uint16_t)(app_data_base + 2), 2)),
                                    rd16(emu.read_debug_memory((uint16_t)(app_data_base + 4), 2)),
                                    rd16(emu.read_debug_memory((uint16_t)(app_data_base + 6), 2)));
                    }
                    if (A.syms.count("_app_bootstrap") != 0) {
                        const uint16_t boot_pc =
                            (uint16_t)(base + A.at("_app_bootstrap") + app_symbol_bias);
                        std::printf("process[0].main.bootstrap.bytes: %s\n",
                                    bytes_hex(emu.read_debug_memory(boot_pc, 96), 96).c_str());
                        const uint16_t app_partos_ptr_addr =
                            rd16(emu.read_debug_memory((uint16_t)(boot_pc + 14), 2));
                        const uint16_t app_libc_ptr_addr =
                            rd16(emu.read_debug_memory((uint16_t)(boot_pc + 36), 2));
                        std::printf("process[0].main.bootstrap.data: app_partos_ptr@%04X=%04X app_libc_ptr@%04X=%04X\n",
                                    app_partos_ptr_addr,
                                    app_partos_ptr_addr == 0
                                        ? 0
                                        : rd16(emu.read_debug_memory(app_partos_ptr_addr, 2)),
                                    app_libc_ptr_addr,
                                    app_libc_ptr_addr == 0
                                        ? 0
                                        : rd16(emu.read_debug_memory(app_libc_ptr_addr, 2)));
                        if (A.syms.count("_app_bootstrap") != 0 &&
                            A.at("_app_bootstrap") <= 0x070c &&
                            A.at("l__CODE") > 0x073f) {
                            const uint16_t call_hl_pc =
                                (uint16_t)(base + 0x070c);
                            const uint16_t call_iy_pc =
                                (uint16_t)(base + 0x073c);
                            std::printf("process[0].main.call_hl.bytes: %s\n",
                                        bytes_hex(emu.read_debug_memory(call_hl_pc, 12), 12).c_str());
                            std::printf("process[0].main.call_iy.bytes: %s\n",
                                        bytes_hex(emu.read_debug_memory(call_iy_pc, 12), 12).c_str());
                        }
                    }
                    if (A.syms.count("_pa_init") != 0) {
                        const uint16_t pa_init_pc =
                            (uint16_t)(base + A.at("_pa_init") + app_symbol_bias);
                        std::printf("process[0].main._pa_init.bytes: %s\n",
                                    bytes_hex(emu.read_debug_memory(pa_init_pc, 8), 8).c_str());
                    }
                    if (A.syms.count("_pa_write_buffer") != 0) {
                        const uint16_t pa_wb_pc =
                            (uint16_t)(base + A.at("_pa_write_buffer") + app_symbol_bias);
                        std::printf("process[0].main._pa_write_buffer.bytes: %s\n",
                                    bytes_hex(emu.read_debug_memory(pa_wb_pc, 8), 8).c_str());
                    }
                    std::printf("process[0].main.startup.bytes: %s\n",
                                bytes_hex(emu.read_debug_memory((uint16_t)(main0 + 6), 10), 10).c_str());
                    std::printf("process[0].main.stack.bytes: %s\n",
                                bytes_hex(emu.read_debug_memory(rd16(t, 4), 24), 24).c_str());
                }
            }
        }
        if (process1 != 0) {
            const auto p = emu.read_debug_memory(process1, 19);
            const uint16_t main1 = rd16(p, 13);
            std::printf("process[1]: next=%04X name='%s' main=%04X cmd=%04X env=%04X\n",
                        rd16(p, 0),
                        read_cstr_bounded(emu, (uint16_t)(process1 + 5), 8).c_str(),
                        main1, rd16(p, 15), rd16(p, 17));
            if (main1 != 0) {
                const auto t = emu.read_debug_memory(main1, 25);
                std::printf("process[1].main: next=%04X wait=%04X num=%u state=%u process=%04X bank=%u\n",
                            rd16(t, 0), rd16(t, 16),
                            t.size() > 18 ? (unsigned)t[18] : 0U,
                            t.size() > 19 ? (unsigned)t[19] : 0U,
                            rd16(t, 22),
                            t.size() > 24 ? (unsigned)t[24] : 0U);
            }
        }
    }
    dump_heap("sys", emu, K.at("__sys_heap"), 32);
    dump_heap("usr", emu, K.at("__usr_heap"), 64);
    std::printf("terminal:\n%s\n", emu.dump_terminal_text().c_str());
    std::printf("raw:\n%s\n", emu.dump_raw_serial_text().c_str());
    return 0;
}
