#include "partner_crt.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <map>
#include <regex>
#include <string>
#include <algorithm>
#include <vector>

namespace {

struct probe_partner_crt : partner_crt {
    using partner_crt::partner_crt;
    using partner_crt::io_write;
    using partner::ctc;
    using partner::pio;
    using partner::sio;
    using partner::sio2;
};

constexpr uint16_t PC_STAGE1_READY   = 0x2003;
constexpr uint16_t STUB_ADDR         = 0x2000;
constexpr uint16_t KERNEL_ENTRY      = 0x0000;
constexpr uint16_t ADDR_MODEL        = 0xDE0E;
constexpr uint16_t ADDR_NVRAM_CACHE  = 0xDE0F;
constexpr uint64_t BOOT_TICK_LIMIT   = 50'000'000ULL;
constexpr uint64_t SHELL_TICK_LIMIT  = 120'000'000ULL;
constexpr uint64_t SHELL_SETTLE_TICKS = 2'000'000ULL;
constexpr size_t   RECENT_PC_LIMIT   = 256;
constexpr uint16_t DEV_DATA_OFF      = 9;
constexpr uint16_t PIO_STATE_PTR_OFF = 2;
constexpr uint16_t SIO_STATE_PTR_OFF = 4;
constexpr uint16_t PROCESS_MAIN_THREAD_OFF = 13;

struct symbol_map {
    std::map<std::string, std::vector<uint16_t>> syms;

    explicit symbol_map(const std::string &path)
    {
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

    uint16_t get(const std::string &name,
                 uint16_t fallback,
                 size_t idx = 0) const
    {
        auto it = syms.find(name);
        if (it == syms.end() || idx >= it->second.size())
            return fallback;
        return it->second[idx];
    }
};

static uint16_t rd16(const std::vector<uint8_t> &v, size_t off = 0);

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
    throw std::runtime_error("area " + area + " not in " + path);
}

static uint64_t env_u64_or(const char *name, uint64_t fallback)
{
    const char *s = std::getenv(name);

    if (s == nullptr || *s == '\0')
        return fallback;
    const unsigned long long parsed = std::strtoull(s, nullptr, 10);
    return parsed == 0ULL ? fallback : (uint64_t)parsed;
}

static uint16_t find_owned_payload(const partner_crt &emu,
                                   uint16_t heap,
                                   uint16_t owner,
                                   size_t limit)
{
    std::vector<uint16_t> seen;
    uint16_t block = heap;
    size_t count = 0;
    while (block != 0 && count < limit) {
        if (std::find(seen.begin(), seen.end(), block) != seen.end())
            return 0;
        seen.push_back(block);
        const auto hdr = emu.read_debug_memory(block, 9);
        if (hdr.size() < 9)
            return 0;
        if ((rd16(hdr, 2) == owner) && ((hdr[4] & 0x01) != 0))
            return (uint16_t)(block + 9);
        block = rd16(hdr, 0);
        ++count;
    }
    return 0;
}

static std::string ascii_preview(const std::vector<uint8_t> &bytes)
{
    std::string out;
    out.reserve(bytes.size());
    for (uint8_t b : bytes) {
        if (b == 0)
            break;
        out.push_back((b >= 32 && b < 127) ? char(b) : '.');
    }
    return out;
}

static std::string hex_preview(const std::vector<uint8_t> &bytes)
{
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(bytes.size() * 3);
    for (size_t i = 0; i < bytes.size(); ++i) {
        if (i != 0)
            out.push_back(' ');
        out.push_back(kHex[(bytes[i] >> 4) & 0x0F]);
        out.push_back(kHex[bytes[i] & 0x0F]);
    }
    return out;
}

static std::string preview_name83(const std::vector<uint8_t> &bytes)
{
    std::string out;
    out.reserve(bytes.size());
    for (uint8_t b : bytes) {
        out.push_back((b >= 32 && b <= 126) ? (char)b : '.');
    }
    return out;
}

static std::string cstr_preview(const std::vector<uint8_t> &bytes)
{
    std::string out;
    for (uint8_t b : bytes) {
        if (b == 0)
            break;
        out.push_back((b >= 32 && b <= 126) ? (char)b : '.');
    }
    return out;
}

struct shell_layout {
    uint16_t com = 0;
    uint16_t xl = 0;
    uint16_t code = 0;
    uint16_t code_size = 0;
    uint16_t entry = 0;
};

static shell_layout read_shell_layout(const partner_crt &emu,
                                      uint16_t usr_heap,
                                      uint16_t owner)
{
    shell_layout out{};
    out.com = find_owned_payload(emu, usr_heap, owner, 256);
    if (out.com == 0)
        return out;
    const auto com = emu.read_debug_memory(out.com, 32);
    if (com.size() < 16 || com[0] != 'C' || com[1] != 'M')
        return out;
    const uint16_t xl_off = rd16(com, 8);
    out.xl = (uint16_t)(out.com + xl_off);
    const auto xl = emu.read_debug_memory(out.xl, 24);
    if (xl.size() < 12 || xl[0] != 'X' || xl[1] != 'L')
        return out;
    const uint16_t entry_off = rd16(xl, 4);
    out.code_size = rd16(xl, 6);
    const uint16_t reloc_count = rd16(xl, 8);
    out.code = (uint16_t)(out.xl + 12 + reloc_count * 4);
    out.entry = (uint16_t)(out.code + entry_off);
    return out;
}

static void dump_shell_state(const char *label,
                             const partner_crt &emu,
                             uint16_t usr_heap,
                             uint16_t owner)
{
    constexpr uint16_t SHELL_OFF_RUN_COMMAND = 0x01AC;
    constexpr uint16_t SHELL_OFF_CALL_OFFSET = 0x00C3;
    constexpr uint16_t SHELL_OFF_PARTOS = 0x026E;

    const shell_layout lay = read_shell_layout(emu, usr_heap, owner);
    if (lay.com == 0) {
        std::printf("%s shell: no process-owned COM block for owner=%04X\n",
                    label, owner);
        return;
    }
    if (lay.code == 0) {
        std::printf("%s shell: owner=%04X com=%04X but COM/XL header invalid\n",
                    label, owner, lay.com);
        return;
    }

    const auto vars = emu.read_debug_memory((uint16_t)(lay.code + SHELL_OFF_PARTOS), 15);
    const auto run = emu.read_debug_memory((uint16_t)(lay.code + SHELL_OFF_RUN_COMMAND), 17);
    const auto cpu = emu.capture_debug_cpu_state();
    std::printf(
        "%s shell: owner=%04X com=%04X xl=%04X code=%04X size=%04X entry=%04X curpc=%04X off=%04X\n",
        label,
        owner,
        lay.com,
        lay.xl,
        lay.code,
        lay.code_size,
        lay.entry,
        emu.get_current_pc(),
        (uint16_t)(emu.get_current_pc() - lay.code));
    std::printf(
        "  vars: partos=%04X tmp=%04X len=%u char=%02X buf='%s'\n",
        rd16(vars, 0),
        rd16(vars, 2),
        vars.size() > 4 ? (unsigned)vars[4] : 0U,
        vars.size() > 5 ? vars[5] : 0,
        ascii_preview(std::vector<uint8_t>(vars.begin() + std::min<size_t>(6, vars.size()),
                                           vars.begin() + std::min<size_t>(15, vars.size()))).c_str());
    std::printf(
        "  run: calloff=%04X run=%04X bytes=%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X | hl=%04X de=%04X bc=%04X sp=%04X\n",
        (uint16_t)(lay.code + SHELL_OFF_CALL_OFFSET),
        (uint16_t)(lay.code + SHELL_OFF_RUN_COMMAND),
        run.size() > 0 ? run[0] : 0,
        run.size() > 1 ? run[1] : 0,
        run.size() > 2 ? run[2] : 0,
        run.size() > 3 ? run[3] : 0,
        run.size() > 4 ? run[4] : 0,
        run.size() > 5 ? run[5] : 0,
        run.size() > 6 ? run[6] : 0,
        run.size() > 7 ? run[7] : 0,
        run.size() > 8 ? run[8] : 0,
        run.size() > 9 ? run[9] : 0,
        run.size() > 10 ? run[10] : 0,
        run.size() > 11 ? run[11] : 0,
        run.size() > 12 ? run[12] : 0,
        run.size() > 13 ? run[13] : 0,
        run.size() > 14 ? run[14] : 0,
        run.size() > 15 ? run[15] : 0,
        run.size() > 16 ? run[16] : 0,
        cpu.hl,
        cpu.de,
        cpu.bc,
        cpu.sp);
}

static void dump_boot_loader_state(const char *label,
                                   const partner_crt &emu,
                                   const symbol_map &O)
{
    const uint16_t event = O.get("boot_event$", 0);
    const uint16_t loader_busy = O.get("boot_loader_busy$", 0);
    if (event == 0 || loader_busy == 0) {
        std::printf("%s boot-loader: symbols unavailable\n", label);
        return;
    }
    const uint16_t cmdpath = (uint16_t)(event + 10);
    const uint16_t cmdname = (uint16_t)(event + 26);
    const uint16_t cmdlen = (uint16_t)(event + 39);
    const uint16_t dbg_rc = (uint16_t)(event + 40);
    const uint16_t dbg_stage = (uint16_t)(event + 42);
    const uint16_t cmdline = (uint16_t)(event + 51);
    const auto line = emu.read_debug_memory(cmdline, 32);
    const auto name = emu.read_debug_memory(cmdname, 16);
    const auto path = emu.read_debug_memory(cmdpath, 16);
    const auto len = emu.read_debug_memory(cmdlen, 1);
    const auto evt = emu.read_debug_memory(event, 2);
    const auto busy = emu.read_debug_memory(loader_busy, 1);
    const auto rc = emu.read_debug_memory(dbg_rc, 2);
    const auto stage = emu.read_debug_memory(dbg_stage, 1);
    std::printf(
        "%s boot-loader: len=%u busy=%u evt=%04X rc=%04X stage=%02X cmdline='%s' name='%s' path='%s'\n",
        label,
        len.empty() ? 0U : (unsigned)len[0],
        busy.empty() ? 0U : (unsigned)busy[0],
        evt.size() < 2 ? 0U : (unsigned)rd16(evt, 0),
        rc.size() < 2 ? 0U : (unsigned)rd16(rc, 0),
        stage.empty() ? 0U : (unsigned)stage[0],
        cstr_preview(line).c_str(),
        cstr_preview(name).c_str(),
        cstr_preview(path).c_str());
}

static uint16_t rd16(const std::vector<uint8_t> &v, size_t off)
{
    if (off + 1 >= v.size())
        return 0;
    return (uint16_t)(v[off] | (uint16_t(v[off + 1]) << 8));
}

static uint8_t read_event_state(const partner_crt &emu, uint16_t evt)
{
    if (evt == 0)
        return 0xFF;
    const auto e = emu.read_debug_memory((uint16_t)(evt + 4), 1);
    return e.empty() ? 0xFF : e[0];
}

static void dump_drv_state(const char *label, const partner_crt &emu, uint16_t addr, size_t size)
{
    if (addr == 0) {
        std::printf("%s: 0000\n", label);
        return;
    }
    const auto s = emu.read_debug_memory(addr, size);
    if (s.size() < 35) {
        std::printf("%s: %04X <short read>\n", label, addr);
        return;
    }
    std::printf(
        "%s: %04X dev=%04X lock=%04X rdleft=%04X rdevt=%04X rdowner=%04X wrleft=%04X wrevt=%04X wrowner=%04X wract=%02X txcount=%02X\n",
        label,
        addr,
        rd16(s, 0),
        rd16(s, 4),
        rd16(s, 20),
        rd16(s, 22),
        rd16(s, 24),
        rd16(s, 28),
        rd16(s, 30),
        rd16(s, 32),
        s[34],
        s[17]);
}

static void dump_live_sio(const char *label, const probe_partner_crt &emu)
{
    const auto dump_one = [&](const char *name, const z80sio_channel_t &ch) {
        std::printf(
            "%s %s: wr1=%02X wr3=%02X wr4=%02X wr5=%02X rr0=%02X rr1=%02X rx_ready=%d tx_ready=%d int_state=%02X vec=%02X data=%02X\n",
            label,
            name,
            ch.wr[1],
            ch.wr[3],
            ch.wr[4],
            ch.wr[5],
            ch.rr[0],
            ch.rr[1],
            ch.rx_ready ? 1 : 0,
            ch.tx_ready ? 1 : 0,
            ch.int_state,
            ch.int_vector,
            ch.rx_data);
    };
    dump_one("sio0a", emu.sio.chn[Z80SIO_CHANNEL_A]);
    dump_one("sio0b", emu.sio.chn[Z80SIO_CHANNEL_B]);
    dump_one("sio1a", emu.sio2.chn[Z80SIO_CHANNEL_A]);
    dump_one("sio1b", emu.sio2.chn[Z80SIO_CHANNEL_B]);
}

static bool shell_prompt_seen(const partner_crt &emu)
{
    const std::string term = emu.dump_terminal_text();
    if (term.find("PARTOS shell") != std::string::npos &&
        term.find('>') != std::string::npos)
        return true;
    const std::string raw = emu.dump_raw_serial_text();
    return raw.find("PARTOS shell") != std::string::npos &&
           raw.find('>') != std::string::npos;
}

static bool build_all(const std::string &root)
{
    if (std::system("make -C " PARTOS_ROOT " -s sys rom") != 0) {
        std::puts("FAIL: PartOS ROM/sys build failed");
        return false;
    }
    if (std::system("python3 " PARTOS_ROOT "/tools/mkdosdisk.py " PARTOS_ROOT "/bin/disks") != 0) {
        std::puts("FAIL: disk image build failed");
        return false;
    }
    return true;
}

static void step_one_instruction(partner &emu)
{
    while (emu.is_opdone())
        emu.tick();

    static constexpr uint32_t STEP_GUARD_LIMIT = 1'000'000;
    uint32_t guard = 0;
    while (!emu.is_opdone() && guard < STEP_GUARD_LIMIT) {
        emu.tick();
        ++guard;
    }
}

static void dump_state(const char *label, const partner_crt &emu)
{
    const auto cpu = emu.capture_debug_cpu_state();
    const auto stack = emu.read_debug_memory(cpu.sp, 8);
    const uint16_t ret0 = stack.size() >= 2 ? (uint16_t)(stack[0] | (stack[1] << 8)) : 0;
    const uint16_t ret1 = stack.size() >= 4 ? (uint16_t)(stack[2] | (stack[3] << 8)) : 0;
    const uint16_t ret2 = stack.size() >= 6 ? (uint16_t)(stack[4] | (stack[5] << 8)) : 0;
    std::printf("%s cpu pc=%04X sp=%04X ret=%04X/%04X/%04X\n",
                label, emu.get_current_pc(), cpu.sp, ret0, ret1, ret2);
    std::printf("%s terminal:\n%s\n", label, emu.dump_terminal_text().c_str());
    std::printf("%s raw:\n%s\n", label, emu.dump_raw_serial_text().c_str());
}

static void dump_thread(const char *label, const partner_crt &emu, uint16_t addr)
{
    if (addr == 0) {
        std::printf("%s: 0000\n", label);
        return;
    }
    const auto t = emu.read_debug_memory(addr, 25);
    if (t.size() < 25) {
        std::printf("%s: %04X <short read>\n", label, addr);
        return;
    }
    const uint16_t wait = rd16(t, 16);
    const uint8_t num = t[18];
    uint16_t evt = 0;
    if (wait != 0 && num != 0) {
        const auto cell = emu.read_debug_memory(wait, 2);
        evt = rd16(cell);
    }
    std::printf(
        "%s: %04X next=%04X sp=%04X wait=%04X num=%u state=%u data=%04X bank=%u\n",
        label,
        addr,
        rd16(t, 0),
        rd16(t, 4),
        wait,
        (unsigned)num,
        (unsigned)t[19],
        rd16(t, 22),
        (unsigned)t[24]);
    const uint16_t sp = rd16(t, 4);
    const auto stk = emu.read_debug_memory(sp, 24);
    std::printf("    stack: %04X/%04X/%04X/%04X ... ret=%04X/%04X\n",
                rd16(stk, 0),
                rd16(stk, 2),
                rd16(stk, 4),
                rd16(stk, 6),
                rd16(stk, 20),
                rd16(stk, 22));
    if (evt != 0) {
        std::printf("    wait event: cell=%04X evt=%04X state=%02X\n",
                    wait, evt, read_event_state(emu, evt));
    }
}

static void dump_process(const char *label, const partner_crt &emu, uint16_t addr)
{
    if (addr == 0) {
        std::printf("%s: 0000\n", label);
        return;
    }
    const auto p = emu.read_debug_memory(addr, 15);
    if (p.size() < 15) {
        std::printf("%s: %04X <short read>\n", label, addr);
        return;
    }
    std::printf("%s: %04X next=%04X flags=%02X main=%04X name='%c%c%c%c%c%c%c%c'\n",
                label,
                addr,
                rd16(p, 0),
                p[4],
                rd16(p, 13),
                p[5] ? p[5] : '.',
                p[6] ? p[6] : '.',
                p[7] ? p[7] : '.',
                p[8] ? p[8] : '.',
                p[9] ? p[9] : '.',
                p[10] ? p[10] : '.',
                p[11] ? p[11] : '.',
                p[12] ? p[12] : '.');
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

static void dump_owned_heap_blocks(const char *label,
                                   const partner_crt &emu,
                                   uint16_t heap,
                                   uint16_t owner,
                                   size_t limit)
{
    std::vector<uint16_t> seen;
    uint16_t block = heap;
    size_t count = 0;
    std::printf("%s owner=%04X heap=%04X\n", label, owner, heap);
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
        if (rd16(hdr, 2) == owner && (hdr[4] & 0x01) != 0) {
            std::printf("  blk=%04X next=%04X size=%04X dtor=%04X\n",
                        block, rd16(hdr, 0), rd16(hdr, 5), rd16(hdr, 7));
        }
        block = rd16(hdr, 0);
        ++count;
    }
    if (block != 0)
        std::printf("  ... truncated after %zu blocks, next=%04X\n", count, block);
}

static void dump_runtime(const char *label,
                         const partner_crt &emu,
                         const symbol_map &K,
                         const symbol_map &O)
{
    const auto cur = emu.read_debug_memory(K.at("_thread_current"), 2);
    const auto run = emu.read_debug_memory(K.at("_thread_first_running"), 2);
    const auto wait = emu.read_debug_memory(K.at("_thread_first_waiting"), 2);
    const auto term = emu.read_debug_memory(K.at("_thread_first_terminated"), 2);
    const auto proc = emu.read_debug_memory(O.at("_process_first"), 2);
    const auto err = emu.read_debug_memory(O.at("_process_last_error"), 1);
    const auto pstg = emu.read_debug_memory(O.at("_process_last_stage"), 1);
    const auto pres = emu.read_debug_memory(O.at("_process_last_result"), 2);
    const auto bdstg = emu.read_debug_memory(O.at("_boot_debug_stage"), 1);
    const auto bdrc = emu.read_debug_memory(O.at("_boot_debug_rc"), 2);
    const uint16_t boot_fs_addr = O.at("boot_fs$");
    const uint16_t boot_event_addr =
        O.get("boot_event$", (uint16_t)(boot_fs_addr - 0x78));
    const uint16_t boot_file_addr =
        O.get("boot_file$", (uint16_t)(boot_fs_addr + 30));
    const uint16_t boot_fs_ready_addr =
        O.get("boot_fs_ready$", (uint16_t)(boot_fs_addr - 0x70));
    const uint16_t boot_loader_busy_addr =
        O.get("boot_loader_busy$", (uint16_t)(boot_fs_addr - 0x50));
    const auto bootfs = emu.read_debug_memory(boot_fs_ready_addr, 1);
    const auto bootbusy = emu.read_debug_memory(boot_loader_busy_addr, 1);
    const auto bootevt = emu.read_debug_memory(boot_event_addr, 2);
    const uint16_t bootevt_ptr = rd16(bootevt);
    const auto bootfile_meta = emu.read_debug_memory(boot_file_addr, 12);
    const auto bootfile = emu.read_debug_memory((uint16_t)(boot_file_addr + 10), 2);
    const uint16_t boot_image_addr =
        O.get("boot_image$", (uint16_t)(boot_fs_addr - 0x4e));
    const auto bootimage = emu.read_debug_memory(boot_image_addr, 2);
    const auto bootfs_words = emu.read_debug_memory(boot_fs_addr, 30);
    const auto fat_work_fs = emu.read_debug_memory(O.at("fat_work_fs$"), 2);
    const auto fat_work_dev = emu.read_debug_memory(O.at("fat_work_dev$"), 2);
    const auto fat_work_evt = emu.read_debug_memory(O.at("fat_work_event$"), 2);
    const auto fat_io_evt = emu.read_debug_memory(O.at("fat_io_event$"), 2);
    const auto fat_queue_evt = emu.read_debug_memory(O.at("fat_queue_event$"), 2);
    const auto tick_hook = emu.read_debug_memory(K.at("__sys_vec_tick"), 2);
    const auto fat_lookup_path = emu.read_debug_memory(O.at("fat_lookup_path$"), 2);
    const uint16_t fat_lookup_path_ptr = rd16(fat_lookup_path);
    const auto fat_lookup_dirent = emu.read_debug_memory(O.at("fat_lookup_dirent$"), 2);
    const uint16_t fat_lookup_dirent_ptr = rd16(fat_lookup_dirent);
    const auto fat_lookup_sector = emu.read_debug_memory(O.at("fat_lookup_sector$"), 2);
    const auto fat_scan_remaining = emu.read_debug_memory(O.at("fat_scan_remaining$"), 2);
    const auto fat_sector_head = emu.read_debug_memory(O.at("fat_sector$"), 32);
    const auto fat_lookup_status =
        fat_lookup_dirent_ptr != 0
            ? emu.read_debug_memory((uint16_t)(fat_lookup_dirent_ptr + 10), 2)
            : std::vector<uint8_t>{};
    const auto fat_name = emu.read_debug_memory(O.at("fat_name$"), 11);
    const auto boot_cmd = emu.read_debug_memory((uint16_t)(boot_fs_addr - 24), 16);
    const auto fat_lookup_path_text =
        fat_lookup_path_ptr != 0
            ? emu.read_debug_memory(fat_lookup_path_ptr, 16)
            : std::vector<uint8_t>{};
    const auto &ctc = emu.get_ctc();
    const auto cpu = emu.capture_debug_cpu_state();
    const auto ir_ref = emu.read_debug_memory(K.at("ir_refcnt"), 1);
    const auto ir_armed = emu.read_debug_memory(K.at("ir_armed"), 1);
    const uint16_t pio0_dev = O.at("pio_dev0");
    const auto pio1_dev_ptr = emu.read_debug_memory(pio0_dev, 2);
    const uint16_t pio1_dev = rd16(pio1_dev_ptr);
    const uint16_t sio0_dev = O.at("sio_dev0");
    const uint16_t sio1_dev = O.at("sio_dev1");
    const auto pio0_state = emu.read_debug_memory((uint16_t)(pio0_dev + DEV_DATA_OFF + PIO_STATE_PTR_OFF), 2);
    const auto pio1_state = emu.read_debug_memory((uint16_t)(pio1_dev + DEV_DATA_OFF + PIO_STATE_PTR_OFF), 2);
    const auto sio0_state = emu.read_debug_memory((uint16_t)(sio0_dev + DEV_DATA_OFF + SIO_STATE_PTR_OFF), 2);
    const auto sio1_state = emu.read_debug_memory((uint16_t)(sio1_dev + DEV_DATA_OFF + SIO_STATE_PTR_OFF), 2);
    std::printf("%s runtime: current=%04X running=%04X waiting=%04X terminated=%04X process=%04X perror=%02X pstage=%02X presult=%04X bstage=%02X brc=%04X fs_ready=%02X loader_busy=%02X\n",
                label,
                rd16(cur),
                rd16(run),
                rd16(wait),
                rd16(term),
                rd16(proc),
                err.empty() ? 0xFF : err[0],
                pstg.empty() ? 0xFF : pstg[0],
                rd16(pres),
                bdstg.empty() ? 0xFF : bdstg[0],
                rd16(bdrc),
                bootfs.empty() ? 0xFF : bootfs[0],
                bootbusy.empty() ? 0xFF : bootbusy[0]);
    std::printf("  boot: event_cell=%04X evt=%04X evt_state=%02X file_status=%04X\n",
                boot_event_addr,
                bootevt_ptr,
                read_event_state(emu, bootevt_ptr),
                rd16(bootfile));
    if (bootfile_meta.size() >= 12) {
        std::printf("  bootfile: cluster=%04X size=%02X%02X%02X%02X dir_sector=%04X dir_off=%02X attr=%02X image=%04X\n",
                    (uint16_t)(bootfile_meta[0] | (uint16_t(bootfile_meta[1]) << 8)),
                    bootfile_meta[5],
                    bootfile_meta[4],
                    bootfile_meta[3],
                    bootfile_meta[2],
                    (uint16_t)(bootfile_meta[6] | (uint16_t(bootfile_meta[7]) << 8)),
                    bootfile_meta[8],
                    bootfile_meta[9],
                    rd16(bootimage));
    }
    if (bootfs_words.size() >= 30) {
        auto fsw = [&](size_t off) -> uint16_t {
            return (uint16_t)(bootfs_words[off] | (uint16_t(bootfs_words[off + 1]) << 8));
        };
        std::printf("  bootfs: dev=%04X lba=%04X total=%04X reserved=%04X spf=%04X root_entries=%04X root_secs=%04X fat_start=%04X root_start=%04X data_start=%04X clusters=%04X hint=%04X spc=%02X fats=%02X bits=%02X mounted=%02X status=%04X\n",
                    fsw(0),
                    fsw(2),
                    fsw(4),
                    fsw(6),
                    fsw(8),
                    fsw(10),
                    fsw(12),
                    fsw(14),
                    fsw(16),
                    fsw(18),
                    fsw(20),
                    fsw(22),
                    bootfs_words[24],
                    bootfs_words[25],
                    bootfs_words[26],
                    bootfs_words[27],
                    fsw(28));
    }
    std::printf("  fat: work_fs=%04X work_dev=%04X work_evt=%04X evt_state=%02X io_evt=%04X io_state=%02X lookup_path=%04X lookup_dirent=%04X lookup_status=%04X\n",
                rd16(fat_work_fs),
                rd16(fat_work_dev),
                rd16(fat_work_evt),
                read_event_state(emu, rd16(fat_work_evt)),
                rd16(fat_io_evt),
                read_event_state(emu, rd16(fat_io_evt)),
                fat_lookup_path_ptr,
                fat_lookup_dirent_ptr,
                rd16(fat_lookup_status));
    std::printf("  fatscan: sector=%04X remaining=%04X head='%s'\n",
                rd16(fat_lookup_sector),
                rd16(fat_scan_remaining),
                ascii_preview(fat_sector_head).c_str());
    std::printf("  fat names: boot_cmd='%s' lookup_ptr='%s' short='%.*s'\n",
                ascii_preview(boot_cmd).c_str(),
                ascii_preview(fat_lookup_path_text).c_str(),
                (int)fat_name.size(),
                fat_name.empty() ? "" : reinterpret_cast<const char *>(fat_name.data()));
    std::printf("  fat path bytes:");
    for (uint8_t b : fat_lookup_path_text)
        std::printf(" %02X", b);
    std::printf("\n");
    std::printf("  fatq: queue_evt=%04X evt_state=%02X\n",
                rd16(fat_queue_evt),
                read_event_state(emu, rd16(fat_queue_evt)));
    std::printf("  ctc: ch2 ctrl=%02X const=%02X down=%02X wait=%d int=%02X vec=%02X | ch3 ctrl=%02X const=%02X down=%02X wait=%d int=%02X vec=%02X | im2_ack=%u\n",
                ctc.chn[2].control,
                ctc.chn[2].constant,
                ctc.chn[2].down_counter,
                ctc.chn[2].waiting_for_trigger ? 1 : 0,
                ctc.chn[2].int_state,
                ctc.chn[2].int_vector,
                ctc.chn[3].control,
                ctc.chn[3].constant,
                ctc.chn[3].down_counter,
                ctc.chn[3].waiting_for_trigger ? 1 : 0,
                ctc.chn[3].int_state,
                ctc.chn[3].int_vector,
                emu.dbg_im2_ack_count);
    std::printf("  ir: iff1=%d iff2=%d ref=%02X armed=%02X irref_writes=%u\n",
                cpu.iff1 ? 1 : 0,
                cpu.iff2 ? 1 : 0,
                ir_ref.empty() ? 0xFF : ir_ref[0],
                ir_armed.empty() ? 0xFF : ir_armed[0],
                emu.dbg_irref_count);
    std::printf("  tick: hook=%04X\n", rd16(tick_hook));
    if (emu.dbg_irref_count != 0) {
        const uint32_t total = emu.dbg_irref_count;
        const uint32_t start = total > 6 ? (total - 6) : 0;
        std::printf("  ir writes:");
        for (uint32_t idx = start; idx < total; ++idx) {
            const uint32_t ring = idx & 0xFu;
            std::printf(" [%u]=%02X@%04X sp=%04X stk=%04X/%04X",
                        idx,
                        emu.dbg_irref_values[ring],
                        emu.dbg_irref_pcs[ring],
                        emu.dbg_irref_sps[ring],
                        emu.dbg_irref_stack0[ring],
                        emu.dbg_irref_stack1[ring]);
        }
        std::printf("\n");
    }
    dump_thread("  current", emu, rd16(cur));
    dump_thread("  running", emu, rd16(run));
    dump_thread("  waiting", emu, rd16(wait));
    if (rd16(wait) != 0) {
        const auto wait_hdr = emu.read_debug_memory(rd16(wait), 2);
        dump_thread("  waiting.next", emu, rd16(wait_hdr));
    }
    dump_thread("  terminated", emu, rd16(term));
    dump_process("  process", emu, rd16(proc));
    dump_drv_state("  pio0", emu, rd16(pio0_state), 37);
    dump_drv_state("  pio1", emu, rd16(pio1_state), 37);
    dump_drv_state("  sio0", emu, rd16(sio0_state), 38);
    dump_drv_state("  sio1", emu, rd16(sio1_state), 38);
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

    const std::string rom_path = PARTOS_ROOT "/bin/partos.rom";
    const std::string kernel_path = PARTOS_ROOT "/bin/kernel.sys";
    const std::string os_path = PARTOS_ROOT "/bin/os.sys";
    const std::string hdd_path = PARTOS_ROOT "/bin/disks/hdd-dos.img";
    const std::string kernel_map_path = PARTOS_ROOT "/build/kernel.map";
    const std::string os_map_path = PARTOS_ROOT "/build/os.map";
    symbol_map K(kernel_map_path);
    symbol_map O(os_map_path);
    const uint16_t sys_kernel = K.at("__sys_kernel");
    const uint16_t os_entry = O.at("__os_entry");
    const uint16_t os_drv_register_all = O.at("__drv_register_all");
    const uint16_t os_dev_init = O.at("__dev_init");
    const uint16_t os_dev_init_all = O.at("__dev_init_all");
    const uint16_t os_dev_probe_all = O.at("__dev_probe_all");
    const uint16_t os_console_init = O.at("_console_init");
    const uint16_t os_syscall_init = O.at("_syscall_init");
    const uint16_t os_bootstrap = O.at("_kernel_bootstrap");
    const uint16_t os_boot_after_evt_create = O.at("__boot_after_evt_create");
    const uint16_t os_boot_try_sda = O.at("__boot_try_sda");
    const uint16_t os_boot_try_fd0 = O.at("__boot_try_fd0");
    const uint16_t os_boot_try_path = O.at("boot_try_path$");
    const uint16_t os_boot_cleanup = O.at("__boot_cleanup");
    const uint16_t os_boot_exit = O.at("__boot_exit");
    const uint16_t os_ctc_enable_tick = O.at("ctc_enable_tick");
    const uint16_t fat_init = O.at("_fat_init");
    const uint16_t fat_init_busy = O.at("fi_busy$");
    const uint16_t fat_init_nomem = O.at("fi_nomem$");
    const uint16_t fat_init_fail_queue_evt = O.at("fi_fail_queue_evt$");
    const uint16_t fat_init_fail_both_evts = O.at("fi_fail_both_evts$");
    const uint16_t fat_init_after_guard_alloc = O.at("fi_after_guard_alloc$");
    const uint16_t fat_mount = O.at("_fat_mount");
    const uint16_t mem_allocate = K.at("_mem_allocate");
    const uint16_t thread_create = K.at("_thread_create");
    const uint16_t thread_create_fail0 = K.at("tc_fail0$");
    const uint16_t thread_create_fail1 = K.at("tc_fail1$");
    const uint16_t fat_handle_mount = O.at("fat_handle_mount$");
    const uint16_t fat_complete_fs = O.at("fat_complete_fs$");
    const uint16_t fat_open = O.at("_fat_open");
    const uint16_t fat_read = O.at("_fat_read");
    const uint16_t process_load_com = O.at("_process_load_com");
    const bool direct_split_boot =
        std::getenv("IDP_DIRECT_SPLIT_BOOT") != nullptr;
    const bool trace_preprompt =
        std::getenv("IDP_TRACE_PREPROMPT") != nullptr;
    const bool trace_preprompt_verbose =
        std::getenv("IDP_TRACE_PREPROMPT_VERBOSE") != nullptr;
    static const std::array<uint8_t, 8> k_nvram = {
        0x00, 0x40, 0x80, 0x3F, 0x00, 0x00, 0x00, 0x02,
    };
    uint16_t boot_shell_addr = 0;
    std::vector<uint8_t> boot_shell_expected;
    bool boot_shell_change_logged = false;

    probe_partner_crt emu(terminal_profile::vt52, PARTOS_ROOT "/partos_shadow_nvram.bin");
    emu.load_hdd(hdd_path);
    uint64_t guard = 0;
    auto st = emu.capture_debug_cpu_state();
    if (direct_split_boot) {
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
        static const std::array<uint8_t, 17> k_boot_shell_bytes = {
            '/', 'S', 'H', 'E', 'L', 'L', '.', 'C', 'O', 'M', 0,
            's', 'h', 'e', 'l', 'l', 0,
        };
        const auto boot_shell_it = std::search(os_img.begin(), os_img.end(),
                                               k_boot_shell_bytes.begin(),
                                               k_boot_shell_bytes.end());
        if (boot_shell_it != os_img.end()) {
            boot_shell_addr =
                (uint16_t)(os_base + std::distance(os_img.begin(), boot_shell_it));
            boot_shell_expected.assign(k_boot_shell_bytes.begin(),
                                       k_boot_shell_bytes.end());
        }
        emu.reset();
        emu.clean_kernel_io_handoff();
        emu.seed_cmos_nvram(k_nvram.data(), k_nvram.size());
        emu.write_debug_memory(os_base, os_img);
        const std::vector<uint8_t> stub = {
            0xD3, 0x80,                         // out (0x80),a -> disable ROM
            0x76,                               // halt
        };
        emu.write_debug_memory(STUB_ADDR, stub);
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
        for (guard = 0; guard < 1000; ++guard) {
            emu.tick();
            if (!emu.is_rom_enabled() && emu.capture_debug_cpu_state().halted)
                break;
        }
        if (emu.is_rom_enabled()) {
            std::puts("FAIL: direct shell probe never disabled the ROM overlay");
            return 1;
        }
        emu.write_debug_memory(kernel_base, kernel_img);
        st = emu.capture_debug_cpu_state();
        st.halted = false;
        emu.apply_debug_cpu_state(st);
        emu.debug_set_pc(KERNEL_ENTRY);
    } else {
        emu.load_rom(rom_path);
        emu.reset();
        while (emu.is_rom_enabled() || emu.get_current_pc() != PC_STAGE1_READY) {
            emu.tick();
            if (++guard > BOOT_TICK_LIMIT) {
                std::printf("FAIL: never reached stage-1 ready state (pc=%04X rom=%d)\n",
                            emu.get_current_pc(), emu.is_rom_enabled() ? 1 : 0);
                return 1;
            }
        }
        emu.seed_cmos_nvram(k_nvram.data(), k_nvram.size());
        emu.write_debug_memory(ADDR_MODEL, {0x00});
        emu.write_debug_memory(ADDR_NVRAM_CACHE,
                               std::vector<uint8_t>(k_nvram.begin(), k_nvram.end()));
        st = emu.capture_debug_cpu_state();
        st.sp = 0xBFFF;
        st.halted = false;
        emu.apply_debug_cpu_state(st);
    }

    bool hit_page0 = false;
    bool hit_kernel = false;
    bool hit_os_entry = false;
    bool hit_drv_register_all = false;
    bool hit_dev_init = false;
    bool hit_dev_init_all = false;
    bool hit_dev_probe_all = false;
    bool hit_console_init = false;
    bool hit_syscall_init = false;
    bool hit_bootstrap = false;
    bool hit_boot_after_evt_create = false;
    bool hit_boot_try_sda = false;
    bool hit_boot_try_fd0 = false;
    bool hit_boot_try_path = false;
    bool hit_boot_cleanup = false;
    bool hit_boot_exit = false;
    bool hit_ctc_enable_tick = false;
    bool hit_fat_init = false;
    bool hit_fat_mount = false;
    bool hit_fat_open = false;
    bool hit_fat_read = false;
    bool hit_process_load_com = false;
    bool hit_fat_init_busy = false;
    bool hit_fat_init_nomem = false;
    bool hit_fat_init_fail_queue_evt = false;
    bool hit_fat_init_fail_both_evts = false;
    bool hit_fat_init_after_guard_alloc = false;
    uint32_t trace_mem_allocate_guard_hits = 0;
    uint32_t trace_thread_create_hits = 0;
    uint32_t trace_thread_create_fail0_hits = 0;
    uint32_t trace_thread_create_fail1_hits = 0;
    bool trace_shell_saved_logged = false;
    uint64_t tick_page0 = 0;
    uint64_t tick_kernel = 0;
    uint64_t tick_os_entry = 0;
    uint64_t tick_drv_register_all = 0;
    uint64_t tick_dev_init = 0;
    uint64_t tick_dev_init_all = 0;
    uint64_t tick_dev_probe_all = 0;
    uint64_t tick_console_init = 0;
    uint64_t tick_syscall_init = 0;
    uint64_t tick_bootstrap = 0;
    uint64_t tick_boot_after_evt_create = 0;
    uint64_t tick_boot_try_sda = 0;
    uint64_t tick_boot_try_fd0 = 0;
    uint64_t tick_boot_try_path = 0;
    uint64_t tick_boot_cleanup = 0;
    uint64_t tick_boot_exit = 0;
    uint64_t tick_ctc_enable_tick = 0;
    uint64_t tick_fat_init = 0;
    uint64_t tick_fat_mount = 0;
    uint64_t tick_fat_open = 0;
    uint64_t tick_fat_read = 0;
    uint64_t tick_process_load_com = 0;
    uint16_t boot_cleanup_de = 0;
    uint16_t boot_cleanup_hl = 0;
    uint16_t boot_cleanup_sp = 0;
    uint8_t boot_cleanup_plerr = 0xFF;
    const uint16_t usr_heap = K.at("__usr_heap");
    const uint16_t thread_exit_pc = K.at("_thread_exit");
    uint16_t prev_process_first = 0xFFFF;
    uint8_t prev_process_stage = 0xFF;
    uint8_t prev_process_error = 0xFF;
    uint16_t prev_process_result = 0xFFFF;
    uint16_t shell_entry_pc = 0;
    bool hit_shell_entry = false;
    uint32_t trace_fat_handle_mount_hits = 0;
    uint32_t trace_fat_complete_fs_hits = 0;

    const uint64_t prompt_limit =
        trace_preprompt ? 2'000'000ULL : SHELL_TICK_LIMIT;
    for (guard = 0; guard < prompt_limit; ++guard) {
        if (trace_preprompt)
            step_one_instruction(emu);
        else
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
        if (!hit_console_init && pc == os_console_init) {
            hit_console_init = true;
            tick_console_init = emu.get_tick_count();
        }
        if (!hit_syscall_init && pc == os_syscall_init) {
            hit_syscall_init = true;
            tick_syscall_init = emu.get_tick_count();
        }
        if (!hit_bootstrap && pc == os_bootstrap) {
            hit_bootstrap = true;
            tick_bootstrap = emu.get_tick_count();
        }
        if (!hit_boot_after_evt_create && pc == os_boot_after_evt_create) {
            hit_boot_after_evt_create = true;
            tick_boot_after_evt_create = emu.get_tick_count();
        }
        if (!hit_boot_try_sda && pc == os_boot_try_sda) {
            hit_boot_try_sda = true;
            tick_boot_try_sda = emu.get_tick_count();
        }
        if (!hit_boot_try_fd0 && pc == os_boot_try_fd0) {
            hit_boot_try_fd0 = true;
            tick_boot_try_fd0 = emu.get_tick_count();
        }
        if (!hit_boot_try_path && pc == os_boot_try_path) {
            hit_boot_try_path = true;
            tick_boot_try_path = emu.get_tick_count();
        }
        if (!hit_boot_cleanup && pc == os_boot_cleanup) {
            hit_boot_cleanup = true;
            tick_boot_cleanup = emu.get_tick_count();
            const auto cpu = emu.capture_debug_cpu_state();
            boot_cleanup_de = cpu.de;
            boot_cleanup_hl = cpu.hl;
            boot_cleanup_sp = cpu.sp;
            const auto plerr = emu.read_debug_memory(O.at("_process_last_error"), 1);
            if (!plerr.empty())
                boot_cleanup_plerr = plerr[0];
            if (trace_preprompt) {
                std::printf("trace: boot_cleanup de=%04X hl=%04X sp=%04X plerr=%02X tick=%llu\n",
                            boot_cleanup_de, boot_cleanup_hl, boot_cleanup_sp,
                            boot_cleanup_plerr,
                            (unsigned long long)tick_boot_cleanup);
            }
        }
        if (!hit_boot_exit && pc == os_boot_exit) {
            hit_boot_exit = true;
            tick_boot_exit = emu.get_tick_count();
        }
        if (!hit_ctc_enable_tick && pc == os_ctc_enable_tick) {
            hit_ctc_enable_tick = true;
            tick_ctc_enable_tick = emu.get_tick_count();
            if (trace_preprompt) {
                std::printf("trace: ctc_enable_tick tick=%llu\n",
                            (unsigned long long)tick_ctc_enable_tick);
            }
        }
        if (!hit_fat_init && pc == fat_init) {
            hit_fat_init = true;
            tick_fat_init = emu.get_tick_count();
        }
        if (trace_preprompt && !hit_fat_init_busy && pc == fat_init_busy) {
            hit_fat_init_busy = true;
            std::printf("trace: fat_init busy tick=%llu\n",
                        (unsigned long long)emu.get_tick_count());
        }
        if (trace_preprompt && !hit_fat_init_nomem && pc == fat_init_nomem) {
            hit_fat_init_nomem = true;
            std::printf("trace: fat_init nomem tick=%llu\n",
                        (unsigned long long)emu.get_tick_count());
        }
        if (trace_preprompt && !hit_fat_init_fail_queue_evt &&
            pc == fat_init_fail_queue_evt) {
            hit_fat_init_fail_queue_evt = true;
            std::printf("trace: fat_init fail_queue_evt tick=%llu\n",
                        (unsigned long long)emu.get_tick_count());
        }
        if (trace_preprompt && !hit_fat_init_fail_both_evts &&
            pc == fat_init_fail_both_evts) {
            hit_fat_init_fail_both_evts = true;
            std::printf("trace: fat_init fail_both_evts tick=%llu\n",
                        (unsigned long long)emu.get_tick_count());
        }
        if (trace_preprompt && !hit_fat_init_after_guard_alloc &&
            pc == fat_init_after_guard_alloc) {
            hit_fat_init_after_guard_alloc = true;
            const auto cpu = emu.capture_debug_cpu_state();
            std::printf("trace: fat_init after_guard_alloc de=%04X hl=%04X bc=%04X sp=%04X tick=%llu\n",
                        cpu.de,
                        cpu.hl,
                        cpu.bc,
                        cpu.sp,
                        (unsigned long long)emu.get_tick_count());
        }
        if (trace_preprompt && pc == mem_allocate &&
            trace_mem_allocate_guard_hits < 4) {
            const auto cpu = emu.capture_debug_cpu_state();
            if (cpu.de == 0x2000) {
                ++trace_mem_allocate_guard_hits;
                std::printf("trace: mem_allocate guard #%u hl=%04X de=%04X bc=%04X sp=%04X tick=%llu\n",
                            trace_mem_allocate_guard_hits,
                            cpu.hl,
                            cpu.de,
                            cpu.bc,
                            cpu.sp,
                            (unsigned long long)emu.get_tick_count());
            }
        }
        if (trace_preprompt && pc == thread_create &&
            trace_thread_create_hits < 6) {
            ++trace_thread_create_hits;
            const auto cpu = emu.capture_debug_cpu_state();
            std::printf("trace: thread_create #%u hl=%04X de=%04X bc=%04X sp=%04X tick=%llu\n",
                        trace_thread_create_hits,
                        cpu.hl,
                        cpu.de,
                        cpu.bc,
                            cpu.sp,
                            (unsigned long long)emu.get_tick_count());
        }
        if (trace_preprompt && pc == thread_create_fail0 &&
            trace_thread_create_fail0_hits < 6) {
            ++trace_thread_create_fail0_hits;
            const auto cpu = emu.capture_debug_cpu_state();
            std::printf("trace: thread_create fail0 #%u hl=%04X de=%04X bc=%04X sp=%04X tick=%llu\n",
                        trace_thread_create_fail0_hits,
                        cpu.hl,
                        cpu.de,
                        cpu.bc,
                        cpu.sp,
                        (unsigned long long)emu.get_tick_count());
        }
        if (trace_preprompt && pc == thread_create_fail1 &&
            trace_thread_create_fail1_hits < 6) {
            ++trace_thread_create_fail1_hits;
            const auto cpu = emu.capture_debug_cpu_state();
            const auto uhdr = emu.read_debug_memory(K.at("__usr_heap"), 16);
            std::printf("trace: thread_create fail1 #%u hl=%04X de=%04X bc=%04X sp=%04X tick=%llu\n",
                        trace_thread_create_fail1_hits,
                        cpu.hl,
                        cpu.de,
                        cpu.bc,
                        cpu.sp,
                        (unsigned long long)emu.get_tick_count());
            std::printf("trace: usr_heap head");
            for (uint8_t b : uhdr)
                std::printf(" %02X", b);
            std::printf("\n");
        }
        if (!hit_fat_mount && pc == fat_mount) {
            hit_fat_mount = true;
            tick_fat_mount = emu.get_tick_count();
        }
        if (trace_preprompt && pc == fat_handle_mount &&
            trace_fat_handle_mount_hits < 8) {
            ++trace_fat_handle_mount_hits;
            const auto cpu = emu.capture_debug_cpu_state();
            std::printf(
                "trace: fat_handle_mount #%u hl=%04X de=%04X bc=%04X sp=%04X tick=%llu\n",
                trace_fat_handle_mount_hits,
                cpu.hl,
                cpu.de,
                cpu.bc,
                cpu.sp,
                (unsigned long long)emu.get_tick_count());
        }
        if (trace_preprompt && pc == fat_complete_fs &&
            trace_fat_complete_fs_hits < 8) {
            ++trace_fat_complete_fs_hits;
            const auto cpu = emu.capture_debug_cpu_state();
            std::printf(
                "trace: fat_complete_fs #%u hl=%04X de=%04X bc=%04X sp=%04X tick=%llu\n",
                trace_fat_complete_fs_hits,
                cpu.hl,
                cpu.de,
                cpu.bc,
                cpu.sp,
                (unsigned long long)emu.get_tick_count());
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
            if (trace_preprompt) {
                const auto cpu = emu.capture_debug_cpu_state();
                std::printf("trace: process_load_com hl=%04X de=%04X bc=%04X sp=%04X tick=%llu\n",
                            cpu.hl, cpu.de, cpu.bc, cpu.sp,
                            (unsigned long long)tick_process_load_com);
            }
        }
        if (trace_preprompt && !trace_shell_saved_logged) {
            const uint16_t process_first =
                rd16(emu.read_debug_memory(O.at("_process_first"), 2));
            if (process_first != 0) {
                const shell_layout lay =
                    read_shell_layout(emu, usr_heap, process_first);
                if (lay.code != 0 && pc == (uint16_t)(lay.code + 0x00D0)) {
                    trace_shell_saved_logged = true;
                    const auto vars =
                        emu.read_debug_memory((uint16_t)(lay.code + 0x026E), 8);
                    const auto bytes =
                        emu.read_debug_memory((uint16_t)(lay.code + 0x00D0), 8);
                    const auto cpu = emu.capture_debug_cpu_state();
                    std::printf(
                        "trace: shell_call_saved pc=%04X sp=%04X partos=%04X regsvc=%04X tmp=%04X tick=%llu\n",
                        pc,
                        cpu.sp,
                        rd16(vars, 0),
                        rd16(vars, 2),
                        rd16(vars, 4),
                        (unsigned long long)emu.get_tick_count());
                    std::printf("trace: shell_call_saved bytes:");
                    for (uint8_t b : bytes)
                        std::printf(" %02X", b);
                    std::printf("\n");
                }
            }
        }
        if (trace_preprompt && boot_shell_addr != 0 &&
            !boot_shell_change_logged) {
            const auto live =
                emu.read_debug_memory(boot_shell_addr, boot_shell_expected.size());
            if (live != boot_shell_expected) {
                boot_shell_change_logged = true;
                const auto cpu = emu.capture_debug_cpu_state();
                std::printf(
                    "trace: boot_shell bytes changed addr=%04X pc=%04X sp=%04X tick=%llu\n",
                    boot_shell_addr,
                    pc,
                    cpu.sp,
                    (unsigned long long)emu.get_tick_count());
                std::printf("trace: boot_shell expected:");
                for (uint8_t b : boot_shell_expected)
                    std::printf(" %02X", b);
                std::printf("\n");
                std::printf("trace: boot_shell live    :");
                for (uint8_t b : live)
                    std::printf(" %02X", b);
                std::printf("\n");
            }
        }
        if (trace_preprompt_verbose) {
            const uint16_t process_first =
                rd16(emu.read_debug_memory(O.at("_process_first"), 2));
            const auto process_error_raw =
                emu.read_debug_memory(O.at("_process_last_error"), 1);
            const auto process_stage_raw =
                emu.read_debug_memory(O.at("_process_last_stage"), 1);
            const auto process_result_raw =
                emu.read_debug_memory(O.at("_process_last_result"), 2);
            const uint8_t process_error =
                process_error_raw.empty() ? 0xFF : process_error_raw[0];
            const uint8_t process_stage =
                process_stage_raw.empty() ? 0xFF : process_stage_raw[0];
            const uint16_t process_result = rd16(process_result_raw);

            if ((hit_process_load_com || process_first != 0 ||
                 process_stage != 0 || process_error != 0 ||
                 process_result != 0) &&
                (process_first != prev_process_first ||
                 process_stage != prev_process_stage ||
                process_error != prev_process_error ||
                 process_result != prev_process_result)) {
                const auto cpu = emu.capture_debug_cpu_state();
                std::printf(
                    "trace: preprompt tick=%llu pc=%04X proc=%04X perr=%02X pstg=%02X pres=%04X cur=%04X run=%04X term=%04X sp=%04X\n",
                    (unsigned long long)emu.get_tick_count(),
                    pc,
                    process_first,
                    process_error,
                    process_stage,
                    process_result,
                    rd16(emu.read_debug_memory(K.at("_thread_current"), 2)),
                    rd16(emu.read_debug_memory(K.at("_thread_first_running"), 2)),
                    rd16(emu.read_debug_memory(K.at("_thread_first_terminated"), 2)),
                    cpu.sp);
                prev_process_first = process_first;
                prev_process_stage = process_stage;
                prev_process_error = process_error;
                prev_process_result = process_result;
            }

            if (process_first != 0 && shell_entry_pc == 0) {
                const shell_layout lay =
                    read_shell_layout(emu, usr_heap, process_first);
                if (lay.entry != 0) {
                    shell_entry_pc = lay.entry;
                    std::printf(
                        "trace: shell layout owner=%04X com=%04X xl=%04X code=%04X entry=%04X size=%04X\n",
                        process_first,
                        lay.com,
                        lay.xl,
                        lay.code,
                        lay.entry,
                        lay.code_size);
                    dump_process("  trace process", emu, process_first);
                    dump_thread("  trace main", emu,
                                rd16(emu.read_debug_memory(
                                    (uint16_t)(process_first + PROCESS_MAIN_THREAD_OFF), 2)));
                    dump_owned_heap_blocks("  trace owned usr", emu, usr_heap,
                                           process_first, 32);
                }
            }
            if (shell_entry_pc != 0 && !hit_shell_entry && pc == shell_entry_pc) {
                hit_shell_entry = true;
                const auto cpu = emu.capture_debug_cpu_state();
                const auto bytes = emu.read_debug_memory(pc, 24);
                std::printf(
                    "trace: shell entry reached tick=%llu pc=%04X sp=%04X hl=%04X de=%04X bc=%04X\n",
                    (unsigned long long)emu.get_tick_count(),
                    pc,
                    cpu.sp,
                    cpu.hl,
                    cpu.de,
                    cpu.bc);
                std::printf("trace: shell bytes:");
                for (uint8_t b : bytes)
                    std::printf(" %02X", b);
                std::printf("\n");
            }
            if (pc == thread_exit_pc) {
                const auto cpu = emu.capture_debug_cpu_state();
                std::printf(
                    "trace: thread_exit tick=%llu hl=%04X de=%04X sp=%04X cur=%04X\n",
                    (unsigned long long)emu.get_tick_count(),
                    cpu.hl,
                    cpu.de,
                    cpu.sp,
                    rd16(emu.read_debug_memory(K.at("_thread_current"), 2)));
            }
        }
        if (shell_prompt_seen(emu))
            break;
    }
    if (guard >= prompt_limit) {
        std::printf("FAIL: shell prompt not reached (pc=%04X ticks=%llu)\n",
                    emu.get_current_pc(),
                    (unsigned long long)emu.get_tick_count());
        std::printf("prompt-fail milestones:\n");
        std::printf("  [%c] page0 entry        @ 0x%04X tick=%llu\n",
                    hit_page0 ? 'x' : ' ', KERNEL_ENTRY,
                    (unsigned long long)tick_page0);
        std::printf("  [%c] __sys_kernel       @ 0x%04X tick=%llu\n",
                    hit_kernel ? 'x' : ' ', sys_kernel,
                    (unsigned long long)tick_kernel);
        std::printf("  [%c] __os_entry         @ 0x%04X tick=%llu\n",
                    hit_os_entry ? 'x' : ' ', os_entry,
                    (unsigned long long)tick_os_entry);
        std::printf("  [%c] __drv_register_all @ 0x%04X tick=%llu\n",
                    hit_drv_register_all ? 'x' : ' ', os_drv_register_all,
                    (unsigned long long)tick_drv_register_all);
        std::printf("  [%c] __dev_init         @ 0x%04X tick=%llu\n",
                    hit_dev_init ? 'x' : ' ', os_dev_init,
                    (unsigned long long)tick_dev_init);
        std::printf("  [%c] __dev_init_all     @ 0x%04X tick=%llu\n",
                    hit_dev_init_all ? 'x' : ' ', os_dev_init_all,
                    (unsigned long long)tick_dev_init_all);
        std::printf("  [%c] __dev_probe_all    @ 0x%04X tick=%llu\n",
                    hit_dev_probe_all ? 'x' : ' ', os_dev_probe_all,
                    (unsigned long long)tick_dev_probe_all);
        std::printf("  [%c] _console_init      @ 0x%04X tick=%llu\n",
                    hit_console_init ? 'x' : ' ', os_console_init,
                    (unsigned long long)tick_console_init);
        std::printf("  [%c] _syscall_init      @ 0x%04X tick=%llu\n",
                    hit_syscall_init ? 'x' : ' ', os_syscall_init,
                    (unsigned long long)tick_syscall_init);
        std::printf("  [%c] _kernel_bootstrap  @ 0x%04X tick=%llu\n",
                    hit_bootstrap ? 'x' : ' ', os_bootstrap,
                    (unsigned long long)tick_bootstrap);
        std::printf("  [%c] boot after evt     @ 0x%04X tick=%llu\n",
                    hit_boot_after_evt_create ? 'x' : ' ',
                    os_boot_after_evt_create,
                    (unsigned long long)tick_boot_after_evt_create);
        std::printf("  [%c] boot try sda       @ 0x%04X tick=%llu\n",
                    hit_boot_try_sda ? 'x' : ' ', os_boot_try_sda,
                    (unsigned long long)tick_boot_try_sda);
        std::printf("  [%c] boot try fd0       @ 0x%04X tick=%llu\n",
                    hit_boot_try_fd0 ? 'x' : ' ', os_boot_try_fd0,
                    (unsigned long long)tick_boot_try_fd0);
        std::printf("  [%c] boot try path      @ 0x%04X tick=%llu\n",
                    hit_boot_try_path ? 'x' : ' ', os_boot_try_path,
                    (unsigned long long)tick_boot_try_path);
        std::printf("  [%c] boot cleanup       @ 0x%04X tick=%llu\n",
                    hit_boot_cleanup ? 'x' : ' ', os_boot_cleanup,
                    (unsigned long long)tick_boot_cleanup);
        std::printf("  [%c] boot exit          @ 0x%04X tick=%llu\n",
                    hit_boot_exit ? 'x' : ' ', os_boot_exit,
                    (unsigned long long)tick_boot_exit);
        std::printf("  [%c] ctc_enable_tick    @ 0x%04X tick=%llu\n",
                    hit_ctc_enable_tick ? 'x' : ' ', os_ctc_enable_tick,
                    (unsigned long long)tick_ctc_enable_tick);
        std::printf("  [%c] _fat_init          @ 0x%04X tick=%llu\n",
                    hit_fat_init ? 'x' : ' ', fat_init,
                    (unsigned long long)tick_fat_init);
        std::printf("  [%c] _fat_mount         @ 0x%04X tick=%llu\n",
                    hit_fat_mount ? 'x' : ' ', fat_mount,
                    (unsigned long long)tick_fat_mount);
        std::printf("  [%c] _fat_open          @ 0x%04X tick=%llu\n",
                    hit_fat_open ? 'x' : ' ', fat_open,
                    (unsigned long long)tick_fat_open);
        std::printf("  [%c] _fat_read          @ 0x%04X tick=%llu\n",
                    hit_fat_read ? 'x' : ' ', fat_read,
                    (unsigned long long)tick_fat_read);
        std::printf("  [%c] _process_load_com  @ 0x%04X tick=%llu\n",
                    hit_process_load_com ? 'x' : ' ', process_load_com,
                    (unsigned long long)tick_process_load_com);
        std::printf("  boot cleanup regs: de=%04X hl=%04X sp=%04X process_last_error=%02X\n",
                    boot_cleanup_de, boot_cleanup_hl, boot_cleanup_sp,
                    boot_cleanup_plerr);
        dump_state("prompt-fail", emu);
        dump_runtime("prompt-fail", emu, K, O);
        dump_heap("prompt-fail sys", emu, K.at("__sys_heap"), 32);
        dump_heap("prompt-fail usr", emu, K.at("__usr_heap"), 32);
        dump_live_sio("prompt-fail", emu);
        dump_shell_state("prompt-fail", emu, K.at("__usr_heap"),
                         rd16(emu.read_debug_memory(O.at("_process_first"), 2)));
        dump_boot_loader_state("prompt-fail", emu, O);
        return 1;
    }

    // The first visible prompt appears slightly before the OS has fully
    // unwound the bootstrap/reap path. Give the system a short settle window
    // so scripted input lands on the real shell read loop instead of racing
    // late boot cleanup.
    const uint16_t term_head_addr = K.at("_thread_first_terminated");
    const uint16_t ir_ref_addr = K.at("ir_refcnt");
    const uint16_t process_first_addr = O.at("_process_first");
    const uint64_t shell_settle_ticks =
        env_u64_or("IDP_SHELL_SETTLE_TICKS", SHELL_SETTLE_TICKS);
    for (guard = 0; guard < shell_settle_ticks; ++guard) {
        emu.tick();
        const bool term_clear =
            rd16(emu.read_debug_memory(term_head_addr, 2)) == 0;
        const auto ir_ref = emu.read_debug_memory(ir_ref_addr, 1);
        const auto cpu = emu.capture_debug_cpu_state();
        const uint16_t proc =
            rd16(emu.read_debug_memory(process_first_addr, 2));
        const uint16_t main_thread =
            proc == 0 ? 0 : rd16(emu.read_debug_memory((uint16_t)(proc + PROCESS_MAIN_THREAD_OFF), 2));
        const uint16_t current =
            rd16(emu.read_debug_memory(K.at("_thread_current"), 2));
        if (term_clear && !ir_ref.empty() && (ir_ref[0] == 0) && cpu.iff1 &&
            (main_thread != 0) && (current == main_thread))
            break;
    }

    dump_state("initial", emu);
    dump_runtime("initial", emu, K, O);
    dump_live_sio("initial", emu);
    dump_shell_state("initial", emu, K.at("__usr_heap"), rd16(emu.read_debug_memory(O.at("_process_first"), 2)));
    dump_boot_loader_state("initial", emu, O);

    if (std::getenv("IDP_DISABLE_TICK_AFTER_PROMPT") != nullptr) {
        emu.io_write(0xCA, 0x03);
        emu.io_write(0xCB, 0x03);
        std::puts("initial: CTC tick channels reset for post-prompt tracing");
    }

    if (argc > 2) {
        int trace_steps_after_script = 0;
        if (const char *s = std::getenv("IDP_TRACE_STEPS_AFTER_SCRIPT")) {
            trace_steps_after_script = std::atoi(s);
            if (trace_steps_after_script < 0)
                trace_steps_after_script = 0;
        }
        const bool trace_first_reap =
            std::getenv("IDP_TRACE_FIRST_REAP") != nullptr;
        const uint16_t fat_handle_lookup_pc = O.at("fat_handle_lookup$");
        const uint16_t fat_handle_readdir_pc = O.at("fat_handle_readdir$");
        const uint16_t fat_finish_dirent_pc = O.at("fat_finish_dirent$");
        const uint16_t fat_complete_dirent_pc = O.at("fat_complete_dirent$");
        const uint16_t fat_worker_loop_pc = O.at("fat_worker_loop$");
        const uint16_t fat_init_pc = O.at("_fat_init");
        const uint16_t fat_walk_mode_addr = O.at("fat_walk_mode$");
        const uint16_t fat_readdir_index_addr =
            (uint16_t)(fat_walk_mode_addr + 5);
        const uint16_t fat_lookup_dirent_addr = O.at("fat_lookup_dirent$");
        const uint16_t fat_work_event_addr = O.at("fat_work_event$");
        const uint16_t partos_run_command_pc = O.at("_partos_run_command");
        const uint16_t boot_try_path_pc = O.at("boot_try_path$");
        const uint16_t fat_open_pc = O.at("_fat_open");
        const uint16_t fat_read_pc = O.at("_fat_read");
        const uint16_t process_load_com_pc = O.at("_process_load_com");
        const uint16_t process_start_pc = O.at("_process_start");
        const uint16_t process_wait_pc = O.at("_process_wait");
        const uint16_t mem_alloc_pc = K.at("_mem_allocate");
        const uint16_t partos_write_console_pc = O.at("_partos_write_console");
        const uint16_t partos_read_keyboard_pc = O.at("_partos_read_keyboard");
        const uint16_t thread_create_pc = K.at("_thread_create");
        const uint16_t thread_wait_events_pc = K.at("_thread_wait4events");
        const uint16_t so_destroy_pc = K.at("__so_destroy");
        const uint16_t mem_free_pc = K.at("_mem_free");
        const uint16_t pio_purge_owner_pc = O.at("_pio_purge_owner");
        const uint16_t drv_purge_owner_pc = O.at("drv_purge_owner_ix");
        const uint16_t evt_set_pc = K.at("_evt_set");
        const uint16_t thread_robin_pc = K.at("__thread_robin");
        const uint16_t thread_select_next_pc = K.at("__thread_select_next");
        const uint16_t mem_free_owner_scan_pc = (uint16_t)(K.at("__mem_free_owner") + 0x13);
        const uint16_t mem_free_owner_done_pc = (uint16_t)(K.at("__mem_free_owner") + 0x52);
        const uint16_t sys_heap = K.at("__sys_heap");
        const uint16_t usr_heap = K.at("__usr_heap");
        const shell_layout shell0 =
            read_shell_layout(emu, usr_heap, rd16(emu.read_debug_memory(O.at("_process_first"), 2)));
        const uint16_t shell_call_offset_pc = (uint16_t)(shell0.code + 0x006B);
        const uint16_t shell_echo_char_pc = (uint16_t)(shell0.code + 0x00AC);
        const uint16_t shell_run_command_pc = (uint16_t)(shell0.code + 0x008C);
        const uint16_t shell_write_buffer_pc = (uint16_t)(shell0.code + 0x009D);
        const uint16_t old_term =
            rd16(emu.read_debug_memory(K.at("_thread_first_terminated"), 2));
        bool first_reap_traced = false;
        for (int i = 2; i < argc; ++i) {
            constexpr uint16_t k_sys_watch_addr = 0xFD4B;
            constexpr size_t k_sys_watch_len = 0x25;
            uint32_t hits_lookup = 0;
            uint32_t hits_readdir = 0;
            uint32_t hits_finish = 0;
            uint32_t hits_complete = 0;
            uint32_t hits_worker = 0;
            uint32_t hits_fat_init = 0;
            uint32_t hits_run_command = 0;
            uint32_t hits_boot_try_path = 0;
            uint32_t hits_fat_open = 0;
            uint32_t hits_fat_read = 0;
            uint32_t hits_process_load_com = 0;
            uint32_t hits_process_start = 0;
            uint32_t hits_process_wait = 0;
            uint32_t hits_mem_alloc = 0;
            uint32_t hits_write_console = 0;
            uint32_t hits_read_keyboard = 0;
            uint32_t hits_thread_create = 0;
            uint32_t hits_thread_wait_events = 0;
            uint32_t hits_so_destroy = 0;
            uint32_t hits_mem_free = 0;
            uint32_t hits_pio_purge = 0;
            uint32_t hits_drv_purge = 0;
            uint32_t hits_evt_set = 0;
            uint32_t hits_robin = 0;
            uint32_t hits_select_next = 0;
            uint32_t hits_shell_call_offset = 0;
            uint32_t hits_shell_echo_char = 0;
            uint32_t hits_shell_run_command = 0;
            uint32_t hits_shell_write_buffer = 0;
            uint32_t hits_sys_watch = 0;
            uint32_t hits_ir_state = 0;
            uint32_t hits_mfo_scan = 0;
            uint32_t hits_mfo_done = 0;
            bool traced_req_free = false;
            bool tracked_read_logged = false;
            uint16_t last_mfo_hl = 0xFFFF;
            uint16_t tracked_read_buf = 0;
            uint16_t tracked_read_bytes = 0;
            uint16_t last_pc = 0xFFFF;
            uint32_t same_pc = 0;
            uint32_t max_same_pc = 0;
            uint16_t max_same_pc_addr = 0xFFFF;
            std::deque<uint16_t> recent_pcs;
            std::vector<uint8_t> sys_watch =
                emu.read_debug_memory(k_sys_watch_addr, k_sys_watch_len);
            uint8_t prev_ir_ref =
                emu.read_debug_memory(K.at("ir_refcnt"), 1).empty()
                    ? 0xFF
                    : emu.read_debug_memory(K.at("ir_refcnt"), 1)[0];
            int prev_iff1 = emu.capture_debug_cpu_state().iff1 ? 1 : 0;
            auto trace_tick = [&](int idx) {
                emu.tick();
                const uint16_t pc = emu.get_current_pc();
                const auto cpu = emu.capture_debug_cpu_state();
                if (pc != last_pc) {
                    recent_pcs.push_back(pc);
                    if (recent_pcs.size() > RECENT_PC_LIMIT)
                        recent_pcs.pop_front();
                    last_pc = pc;
                    same_pc = 1;
                } else {
                    same_pc++;
                    if (same_pc > max_same_pc) {
                        max_same_pc = same_pc;
                        max_same_pc_addr = pc;
                    }
                }
                const auto ir_ref_now_raw =
                    emu.read_debug_memory(K.at("ir_refcnt"), 1);
                const uint8_t ir_ref_now =
                    ir_ref_now_raw.empty() ? 0xFF : ir_ref_now_raw[0];
                const int iff1_now = cpu.iff1 ? 1 : 0;
                if (((ir_ref_now != prev_ir_ref) || (iff1_now != prev_iff1)) &&
                    (hits_ir_state < 48)) {
                    ++hits_ir_state;
                    std::printf(
                        "%s trace: ir_state #%u pc=%04X sp=%04X ref=%02X->%02X iff1=%d->%d cur=%04X run=%04X wait=%04X\n",
                        argv[i],
                        hits_ir_state,
                        pc,
                        cpu.sp,
                        prev_ir_ref,
                        ir_ref_now,
                        prev_iff1,
                        iff1_now,
                        rd16(emu.read_debug_memory(K.at("_thread_current"), 2)),
                        rd16(emu.read_debug_memory(K.at("_thread_first_running"), 2)),
                        rd16(emu.read_debug_memory(K.at("_thread_first_waiting"), 2)));
                }
                prev_ir_ref = ir_ref_now;
                prev_iff1 = iff1_now;
                const auto sys_watch_now =
                    emu.read_debug_memory(k_sys_watch_addr, k_sys_watch_len);
                if ((sys_watch_now.size() == sys_watch.size()) &&
                    (sys_watch_now != sys_watch) &&
                    (hits_sys_watch < 64)) {
                    ++hits_sys_watch;
                    std::printf(
                        "%s trace: sys_watch #%u pc=%04X sp=%04X cur=%04X run=%04X wait=%04X\n",
                        argv[i],
                        hits_sys_watch,
                        pc,
                        cpu.sp,
                        rd16(emu.read_debug_memory(K.at("_thread_current"), 2)),
                        rd16(emu.read_debug_memory(K.at("_thread_first_running"), 2)),
                        rd16(emu.read_debug_memory(K.at("_thread_first_waiting"), 2)));
                    for (size_t off = 0; off < sys_watch.size(); ++off) {
                        if (sys_watch_now[off] == sys_watch[off])
                            continue;
                        std::printf("  %04X: %02X -> %02X\n",
                                    (uint16_t)(k_sys_watch_addr + off),
                                    sys_watch[off],
                                    sys_watch_now[off]);
                    }
                    sys_watch = sys_watch_now;
                } else if (sys_watch_now.size() == sys_watch.size()) {
                    sys_watch = sys_watch_now;
                }
                if (pc == fat_worker_loop_pc) {
                    ++hits_worker;
                }
                if (pc == fat_init_pc) {
                    ++hits_fat_init;
                    if (hits_fat_init <= 4) {
                        std::printf("%s trace: fat_init hit #%u hl=%04X de=%04X sp=%04X\n",
                                    argv[i], hits_fat_init, cpu.hl, cpu.de, cpu.sp);
                    }
                }
                if (pc == partos_run_command_pc) {
                    ++hits_run_command;
                    if (hits_run_command <= 8) {
                        std::printf("%s trace: run_command hit #%u hl=%04X de=%04X bc=%04X sp=%04X\n",
                                    argv[i], hits_run_command, cpu.hl, cpu.de, cpu.bc, cpu.sp);
                    }
                }
                if (pc == boot_try_path_pc) {
                    ++hits_boot_try_path;
                    if (hits_boot_try_path <= 8) {
                        std::printf("%s trace: boot_try_path hit #%u hl=%04X de=%04X bc=%04X sp=%04X\n",
                                    argv[i], hits_boot_try_path, cpu.hl, cpu.de, cpu.bc, cpu.sp);
                    }
                }
                if (pc == fat_open_pc) {
                    ++hits_fat_open;
                    if (hits_fat_open <= 8) {
                        std::printf("%s trace: fat_open hit #%u hl=%04X de=%04X bc=%04X sp=%04X\n",
                                    argv[i], hits_fat_open, cpu.hl, cpu.de, cpu.bc, cpu.sp);
                    }
                }
                if (pc == fat_read_pc) {
                    ++hits_fat_read;
                    if (hits_fat_read <= 8) {
                        std::printf("%s trace: fat_read hit #%u hl=%04X de=%04X bc=%04X sp=%04X\n",
                                    argv[i], hits_fat_read, cpu.hl, cpu.de, cpu.bc, cpu.sp);
                        if (!tracked_read_logged) {
                            const auto frame = emu.read_debug_memory(cpu.sp, 6);
                            tracked_read_buf = cpu.de;
                            tracked_read_bytes = rd16(frame, 2);
                            tracked_read_logged = true;
                            const uint16_t tail =
                                (uint16_t)(tracked_read_buf + tracked_read_bytes);
                            const auto tail_hdr =
                                emu.read_debug_memory(tail, 12);
                            std::printf(
                                "%s trace: fat_read boundary buf=%04X bytes=%04X tail=%04X hdr=%s\n",
                                argv[i],
                                tracked_read_buf,
                                tracked_read_bytes,
                                tail,
                                hex_preview(tail_hdr).c_str());
                        }
                    }
                }
                if (pc == process_load_com_pc) {
                    ++hits_process_load_com;
                    if (hits_process_load_com <= 4) {
                        std::printf("%s trace: process_load_com hit #%u hl=%04X de=%04X bc=%04X sp=%04X\n",
                                    argv[i], hits_process_load_com, cpu.hl, cpu.de, cpu.bc, cpu.sp);
                        if (tracked_read_logged && cpu.de == tracked_read_buf) {
                            const uint16_t tail =
                                (uint16_t)(tracked_read_buf + tracked_read_bytes);
                            const auto tail_hdr =
                                emu.read_debug_memory(tail, 12);
                            std::printf(
                                "%s trace: process_load_com boundary buf=%04X bytes=%04X tail=%04X hdr=%s\n",
                                argv[i],
                                tracked_read_buf,
                                tracked_read_bytes,
                                tail,
                                hex_preview(tail_hdr).c_str());
                        }
                    }
                }
                if (pc == process_start_pc) {
                    ++hits_process_start;
                    if (hits_process_start <= 8) {
                        std::printf("%s trace: process_start hit #%u hl=%04X de=%04X bc=%04X sp=%04X\n",
                                    argv[i], hits_process_start, cpu.hl, cpu.de, cpu.bc, cpu.sp);
                    }
                }
                if (pc == process_wait_pc) {
                    ++hits_process_wait;
                    if (hits_process_wait <= 8) {
                        std::printf("%s trace: process_wait hit #%u hl=%04X de=%04X bc=%04X sp=%04X\n",
                                    argv[i], hits_process_wait, cpu.hl, cpu.de, cpu.bc, cpu.sp);
                    }
                }
                if (pc == mem_alloc_pc) {
                    ++hits_mem_alloc;
                    if (hits_mem_alloc <= 32) {
                        std::printf("%s trace: mem_allocate hit #%u hl=%04X de=%04X bc=%04X sp=%04X\n",
                                    argv[i], hits_mem_alloc, cpu.hl, cpu.de, cpu.bc, cpu.sp);
                    }
                }
                if (pc == partos_write_console_pc) {
                    ++hits_write_console;
                    if (hits_write_console <= 16) {
                        std::printf("%s trace: write_console #%u hl=%04X de=%04X bc=%04X sp=%04X\n",
                                    argv[i], hits_write_console, cpu.hl, cpu.de, cpu.bc, cpu.sp);
                    }
                }
                if (pc == partos_read_keyboard_pc) {
                    ++hits_read_keyboard;
                    if (hits_read_keyboard <= 16) {
                        std::printf("%s trace: read_keyboard #%u hl=%04X de=%04X bc=%04X sp=%04X\n",
                                    argv[i], hits_read_keyboard, cpu.hl, cpu.de, cpu.bc, cpu.sp);
                    }
                }
                if (pc == thread_create_pc) {
                    ++hits_thread_create;
                    if (hits_thread_create <= 16) {
                        const auto frame = emu.read_debug_memory(cpu.sp, 6);
                        std::printf("%s trace: thread_create hit #%u hl=%04X de=%04X ret=%04X args=%04X/%04X sp=%04X\n",
                                    argv[i], hits_thread_create, cpu.hl, cpu.de,
                                    rd16(frame, 0), rd16(frame, 2), rd16(frame, 4), cpu.sp);
                    }
                }
                if (pc == thread_wait_events_pc) {
                    ++hits_thread_wait_events;
                    if (hits_thread_wait_events <= 64) {
                        const auto frame = emu.read_debug_memory(cpu.sp, 4);
                        const auto evtvec = emu.read_debug_memory(cpu.hl, 2);
                        const uint16_t evt = rd16(evtvec, 0);
                        std::printf(
                            "%s trace: thread_wait4events hit #%u hl=%04X de=%04X bc=%04X sp=%04X cur=%04X num=%u evt0=%04X evt0_state=%02X ret=%04X/%04X\n",
                            argv[i],
                            hits_thread_wait_events,
                            cpu.hl,
                            cpu.de,
                            cpu.bc,
                            cpu.sp,
                            rd16(emu.read_debug_memory(K.at("_thread_current"), 2)),
                            frame.size() > 2 ? (unsigned)frame[2] : 0U,
                            evt,
                            read_event_state(emu, evt),
                            rd16(frame, 0),
                            rd16(frame, 2));
                    }
                }
                if (pc == so_destroy_pc) {
                    ++hits_so_destroy;
                    if (hits_so_destroy <= 12) {
                        const auto frame = emu.read_debug_memory(cpu.sp, 2);
                        std::printf("%s trace: so_destroy hit #%u hl=%04X de=%04X ret=%04X sp=%04X\n",
                                    argv[i], hits_so_destroy, cpu.hl, cpu.de,
                                    rd16(frame, 0), cpu.sp);
                    }
                }
                if (pc == mem_free_pc) {
                    ++hits_mem_free;
                    if (hits_mem_free <= 12) {
                        const auto frame = emu.read_debug_memory(cpu.sp, 2);
                        std::printf("%s trace: mem_free hit #%u hl=%04X de=%04X ret=%04X sp=%04X\n",
                                    argv[i], hits_mem_free, cpu.hl, cpu.de,
                                    rd16(frame, 0), cpu.sp);
                    }
                    if (!traced_req_free && cpu.de == 0xFD54) {
                        traced_req_free = true;
                        std::printf("%s trace: stepping request free from pc=%04X sp=%04X hl=%04X de=%04X bc=%04X\n",
                                    argv[i], pc, cpu.sp, cpu.hl, cpu.de, cpu.bc);
                        for (int step = 0; step < 160; ++step) {
                            const auto s = emu.capture_debug_cpu_state();
                            const auto stack = emu.read_debug_memory(s.sp, 8);
                            const auto hdr = emu.read_debug_memory(0xFD4B, 0x1C);
                            std::printf(
                                "  free %03d pc=%04X sp=%04X af=%04X bc=%04X de=%04X hl=%04X ix=%04X iy=%04X stk=%04X/%04X/%04X/%04X hdr=%s\n",
                                step,
                                emu.get_current_pc(),
                                s.sp,
                                s.af,
                                s.bc,
                                s.de,
                                s.hl,
                                s.ix,
                                s.iy,
                                rd16(stack, 0),
                                rd16(stack, 2),
                                rd16(stack, 4),
                                rd16(stack, 6),
                                hex_preview(hdr).c_str());
                            step_one_instruction(emu);
                            if (emu.get_current_pc() == 0x0134 ||
                                emu.get_current_pc() == 0x0135) {
                                const auto halt = emu.capture_debug_cpu_state();
                                std::printf(
                                    "  free halt pc=%04X sp=%04X af=%04X bc=%04X de=%04X hl=%04X\n",
                                    emu.get_current_pc(),
                                    halt.sp,
                                    halt.af,
                                    halt.bc,
                                    halt.de,
                                    halt.hl);
                                break;
                            }
                        }
                    }
                }
                if (pc == fat_handle_lookup_pc) {
                    ++hits_lookup;
                    if (hits_lookup <= 4) {
                        const auto cpu = emu.capture_debug_cpu_state();
                        std::printf("%s trace: lookup hit #%u hl=%04X de=%04X sp=%04X\n",
                                    argv[i], hits_lookup, cpu.hl, cpu.de, cpu.sp);
                    }
                }
                if (pc == fat_handle_readdir_pc) {
                    ++hits_readdir;
                    if (hits_readdir <= 64) {
                        const auto cpu = emu.capture_debug_cpu_state();
                        const uint16_t idx =
                            rd16(emu.read_debug_memory(fat_readdir_index_addr, 2));
                        const uint16_t dirent =
                            rd16(emu.read_debug_memory(fat_lookup_dirent_addr, 2));
                        const uint16_t evt =
                            rd16(emu.read_debug_memory(fat_work_event_addr, 2));
                        const int16_t status =
                            (int16_t)rd16(emu.read_debug_memory((uint16_t)(dirent + 10), 2));
                        const uint8_t attr =
                            emu.read_debug_memory((uint16_t)(dirent + 9), 1).empty()
                                ? 0xFF
                                : emu.read_debug_memory((uint16_t)(dirent + 9), 1)[0];
                        std::string raw_name =
                            preview_name83(emu.read_debug_memory((uint16_t)(dirent + 14), 11));
                        std::printf(
                            "%s trace: readdir hit #%u hl=%04X de=%04X sp=%04X idx=%u dirent=%04X status=%d attr=%02X name='%s' evt=%04X evt_state=%02X\n",
                            argv[i],
                            hits_readdir,
                            cpu.hl,
                            cpu.de,
                            cpu.sp,
                            (unsigned)idx,
                            dirent,
                            (int)status,
                            attr,
                            raw_name.c_str(),
                            evt,
                            read_event_state(emu, evt));
                    }
                }
                if (pc == fat_finish_dirent_pc) {
                    ++hits_finish;
                    if (hits_finish <= 64) {
                        const auto cpu = emu.capture_debug_cpu_state();
                        const uint16_t idx =
                            rd16(emu.read_debug_memory(fat_readdir_index_addr, 2));
                        const uint16_t dirent =
                            rd16(emu.read_debug_memory(fat_lookup_dirent_addr, 2));
                        const uint16_t evt =
                            rd16(emu.read_debug_memory(fat_work_event_addr, 2));
                        std::printf(
                            "%s trace: finish hit #%u hl=%04X de=%04X bc=%04X sp=%04X idx=%u dirent=%04X evt=%04X evt_state=%02X\n",
                            argv[i],
                            hits_finish,
                            cpu.hl,
                            cpu.de,
                            cpu.bc,
                            cpu.sp,
                            (unsigned)idx,
                            dirent,
                            evt,
                            read_event_state(emu, evt));
                    }
                }
                if (pc == fat_complete_dirent_pc) {
                    ++hits_complete;
                    const auto cpu = emu.capture_debug_cpu_state();
                    if ((hits_complete <= 64) || (cpu.de != 0)) {
                        const uint8_t attr =
                            emu.read_debug_memory((uint16_t)(cpu.hl + 9), 1).empty()
                                ? 0xFF
                                : emu.read_debug_memory((uint16_t)(cpu.hl + 9), 1)[0];
                        std::string raw_name =
                            preview_name83(emu.read_debug_memory((uint16_t)(cpu.hl + 14), 11));
                        std::printf(
                            "%s trace: complete hit #%u hl=%04X de=%04X bc=%04X sp=%04X attr=%02X name='%s' evt_state=%02X\n",
                            argv[i],
                            hits_complete,
                            cpu.hl,
                            cpu.de,
                            cpu.bc,
                            cpu.sp,
                            attr,
                            raw_name.c_str(),
                            read_event_state(emu, cpu.bc));
                    }
                }
                if (pc == evt_set_pc) {
                    ++hits_evt_set;
                    if (hits_evt_set <= 8) {
                        const auto arg = emu.read_debug_memory((uint16_t)(cpu.sp + 2), 1);
                        std::printf("%s trace: evt_set hit #%u hl=%04X arg=%02X sp=%04X\n",
                                    argv[i], hits_evt_set, cpu.hl,
                                    arg.empty() ? 0xFF : arg[0], cpu.sp);
                    }
                }
                if (pc == thread_robin_pc) {
                    ++hits_robin;
                    if (hits_robin <= 16) {
                        std::printf(
                            "%s trace: robin #%u cur=%04X run=%04X wait=%04X sp=%04X iff1=%d ref=%02X\n",
                            argv[i],
                            hits_robin,
                            rd16(emu.read_debug_memory(K.at("_thread_current"), 2)),
                            rd16(emu.read_debug_memory(K.at("_thread_first_running"), 2)),
                            rd16(emu.read_debug_memory(K.at("_thread_first_waiting"), 2)),
                            cpu.sp,
                            cpu.iff1 ? 1 : 0,
                            emu.read_debug_memory(K.at("ir_refcnt"), 1).empty()
                                ? 0xFF
                                : emu.read_debug_memory(K.at("ir_refcnt"), 1)[0]);
                    }
                }
                if (pc == thread_select_next_pc) {
                    ++hits_select_next;
                    if (hits_select_next <= 16) {
                        const uint16_t wait_head =
                            rd16(emu.read_debug_memory(K.at("_thread_first_waiting"), 2));
                        uint16_t wait_cell = 0;
                        uint16_t wait_evt = 0;
                        uint8_t wait_state = 0xFF;
                        if (wait_head != 0) {
                            const auto t = emu.read_debug_memory((uint16_t)(wait_head + 16), 3);
                            wait_cell = rd16(t, 0);
                            if (wait_cell != 0) {
                                wait_evt = rd16(emu.read_debug_memory(wait_cell, 2));
                                wait_state = read_event_state(emu, wait_evt);
                            }
                        }
                        std::printf(
                            "%s trace: select_next #%u cur=%04X run=%04X wait=%04X wait_cell=%04X evt=%04X state=%02X sp=%04X\n",
                            argv[i],
                            hits_select_next,
                            rd16(emu.read_debug_memory(K.at("_thread_current"), 2)),
                            rd16(emu.read_debug_memory(K.at("_thread_first_running"), 2)),
                            wait_head,
                            wait_cell,
                            wait_evt,
                            wait_state,
                            cpu.sp);
                    }
                }
                if (pc == shell_call_offset_pc) {
                    ++hits_shell_call_offset;
                    if (hits_shell_call_offset <= 16) {
                        const auto frame = emu.read_debug_memory(cpu.sp, 6);
                        std::printf("%s trace: shell_call_offset #%u hl=%04X de=%04X bc=%04X sp=%04X ret=%04X/%04X\n",
                                    argv[i], hits_shell_call_offset, cpu.hl, cpu.de, cpu.bc,
                                    cpu.sp, rd16(frame, 0), rd16(frame, 2));
                    }
                }
                if (pc == shell_echo_char_pc) {
                    ++hits_shell_echo_char;
                    if (hits_shell_echo_char <= 16) {
                        std::printf("%s trace: shell_echo_char #%u hl=%04X de=%04X bc=%04X sp=%04X\n",
                                    argv[i], hits_shell_echo_char, cpu.hl, cpu.de, cpu.bc, cpu.sp);
                    }
                }
                if (pc == shell_run_command_pc) {
                    ++hits_shell_run_command;
                    if (hits_shell_run_command <= 8) {
                        std::printf("%s trace: shell_run_command #%u hl=%04X de=%04X bc=%04X sp=%04X\n",
                                    argv[i], hits_shell_run_command, cpu.hl, cpu.de, cpu.bc, cpu.sp);
                        dump_shell_state("  shell_run", emu, usr_heap,
                                         rd16(emu.read_debug_memory(O.at("_process_first"), 2)));
                    }
                }
                if (pc == shell_write_buffer_pc) {
                    ++hits_shell_write_buffer;
                    if (hits_shell_write_buffer <= 16) {
                        std::printf("%s trace: shell_write_buffer #%u hl=%04X de=%04X bc=%04X sp=%04X\n",
                                    argv[i], hits_shell_write_buffer, cpu.hl, cpu.de, cpu.bc, cpu.sp);
                    }
                }
                if (pc == pio_purge_owner_pc) {
                    ++hits_pio_purge;
                }
                if (pc == drv_purge_owner_pc) {
                    ++hits_drv_purge;
                    if (hits_drv_purge <= 4) {
                        std::printf("%s trace: drv_purge hit #%u ix=%04X de=%04X sp=%04X\n",
                                    argv[i], hits_drv_purge, cpu.ix, cpu.de, cpu.sp);
                    }
                }
                if (pc == mem_free_owner_scan_pc) {
                    const bool changed = cpu.hl != last_mfo_hl;
                    if (cpu.hl != last_mfo_hl) {
                        last_mfo_hl = cpu.hl;
                        ++hits_mfo_scan;
                    }
                    if (changed && hits_mfo_scan <= 24) {
                        std::printf("%s trace: mfo_scan #%u hl=%04X de=%04X bc=%04X sp=%04X\n",
                                    argv[i], hits_mfo_scan, cpu.hl, cpu.de, cpu.bc, cpu.sp);
                    }
                }
                if (pc == mem_free_owner_done_pc) {
                    ++hits_mfo_done;
                    if (hits_mfo_done <= 8) {
                        std::printf("%s trace: mfo_done #%u bc=%04X sp=%04X\n",
                                    argv[i], hits_mfo_done, cpu.bc, cpu.sp);
                    }
                }
                if (trace_first_reap && !first_reap_traced &&
                    pc == K.at("__mem_free_owner")) {
                    const auto cpu = emu.capture_debug_cpu_state();
                    if ((cpu.de == old_term) && (cpu.hl == usr_heap)) {
                        first_reap_traced = true;
                        std::printf("%s trace: first reap entry pc=%04X sp=%04X hl=%04X de=%04X bc=%04X\n",
                                    argv[i], pc, cpu.sp, cpu.hl, cpu.de, cpu.bc);
                        for (int step = 0; step < 160; ++step) {
                            const auto before = emu.capture_debug_cpu_state();
                            const auto stack = emu.read_debug_memory(before.sp, 12);
                            std::printf(
                                "  step %03d pc=%04X sp=%04X af=%04X bc=%04X de=%04X hl=%04X ix=%04X iy=%04X stk=%04X/%04X/%04X\n",
                                step,
                                emu.get_current_pc(),
                                before.sp,
                                before.af,
                                before.bc,
                                before.de,
                                before.hl,
                                before.ix,
                                before.iy,
                                rd16(stack, 0),
                                rd16(stack, 2),
                                rd16(stack, 4));
                            step_one_instruction(emu);
                            const uint16_t after_pc = emu.get_current_pc();
                            if ((after_pc == 0x0131) || (after_pc == 0x0132)) {
                                std::printf("  halted into kernel dead loop at pc=%04X\n", after_pc);
                                break;
                            }
                        }
                    }
                }
            };
            for (const char *p = argv[i]; *p != '\0'; ++p) {
                emu.key_input((uint8_t)*p);
                for (int n = 0; n < 4000; ++n)
                    trace_tick(n);
            }
            if (trace_steps_after_script > 0) {
                std::printf("%s trace: stepping %d instructions after scripted input\n",
                            argv[i], trace_steps_after_script);
                for (int step = 0; step < trace_steps_after_script; ++step) {
                    const auto cpu = emu.capture_debug_cpu_state();
                    const auto stack = emu.read_debug_memory(cpu.sp, 6);
                    std::printf("  step %03d pc=%04X sp=%04X af=%04X bc=%04X de=%04X hl=%04X ix=%04X iy=%04X stk=%04X/%04X/%04X\n",
                                step,
                                emu.get_current_pc(),
                                cpu.sp,
                                cpu.af,
                                cpu.bc,
                                cpu.de,
                                cpu.hl,
                                cpu.ix,
                                cpu.iy,
                                rd16(stack, 0),
                                rd16(stack, 2),
                                rd16(stack, 4));
                    step_one_instruction(emu);
                }
            }
            uint32_t post_script_ticks = 2000000U;
            if (const char *s = std::getenv("IDP_POST_SCRIPT_TICKS")) {
                const unsigned long parsed = std::strtoul(s, nullptr, 10);
                if (parsed != 0UL)
                    post_script_ticks = (uint32_t)parsed;
            }
            for (uint32_t n = 0; n < post_script_ticks; ++n) {
                trace_tick(n);
            }
            dump_state(argv[i], emu);
            dump_runtime(argv[i], emu, K, O);
            dump_live_sio(argv[i], emu);
            dump_shell_state(argv[i], emu, usr_heap, rd16(emu.read_debug_memory(O.at("_process_first"), 2)));
            dump_boot_loader_state(argv[i], emu, O);
            if (old_term != 0) {
                dump_owned_heap_blocks("  sys owned blocks", emu, sys_heap, old_term, 64);
                dump_owned_heap_blocks("  usr owned blocks", emu, usr_heap, old_term, 256);
            }
            dump_heap("  sys", emu, sys_heap, 32);
            dump_heap("  usr", emu, usr_heap, 64);
            std::printf("%s trace: fat_lookup=%u fat_finish=%u fat_complete=%u worker=%u fat_init=%u run_command=%u boot_try_path=%u fat_open=%u fat_read=%u process_load_com=%u process_start=%u process_wait=%u mem_alloc=%u write_console=%u read_keyboard=%u thread_create=%u so_destroy=%u mem_free=%u pio_purge=%u drv_purge=%u evt_set=%u robin=%u select_next=%u shell_calloff=%u shell_echo=%u shell_run=%u shell_write=%u max_same_pc=%u@%04X\n",
                        argv[i],
                        hits_lookup,
                        hits_finish,
                        hits_complete,
                        hits_worker,
                        hits_fat_init,
                        hits_run_command,
                        hits_boot_try_path,
                        hits_fat_open,
                        hits_fat_read,
                        hits_process_load_com,
                        hits_process_start,
                        hits_process_wait,
                        hits_mem_alloc,
                        hits_write_console,
                        hits_read_keyboard,
                        hits_thread_create,
                        hits_so_destroy,
                        hits_mem_free,
                        hits_pio_purge,
                        hits_drv_purge,
                        hits_evt_set,
                        hits_robin,
                        hits_select_next,
                        hits_shell_call_offset,
                        hits_shell_echo_char,
                        hits_shell_run_command,
                        hits_shell_write_buffer,
                        max_same_pc,
                        max_same_pc_addr);
            std::printf("%s recent pcs:", argv[i]);
            for (uint16_t pc : recent_pcs)
                std::printf(" %04X", pc);
            std::printf("\n");
        }
        return 0;
    }

    std::string prev_raw = emu.dump_raw_serial_text();
    std::deque<uint16_t> recent_pcs;
    uint16_t last_pc = 0xFFFF;
    for (int n = 0; n < 200000; ++n) {
        emu.tick();
        const uint16_t pc = emu.get_current_pc();
        if (pc != last_pc) {
            recent_pcs.push_back(pc);
            if (recent_pcs.size() > RECENT_PC_LIMIT)
                recent_pcs.pop_front();
            last_pc = pc;
        }
        const std::string raw = emu.dump_raw_serial_text();
        if (raw != prev_raw) {
            const size_t shared = std::min(prev_raw.size(), raw.size());
            size_t split = 0;
            while (split < shared && prev_raw[split] == raw[split])
                ++split;
            const auto cpu = emu.capture_debug_cpu_state();
            const auto stack = emu.read_debug_memory(cpu.sp, 8);
            const uint16_t ret0 = stack.size() >= 2 ? (uint16_t)(stack[0] | (stack[1] << 8)) : 0;
            const uint16_t ret1 = stack.size() >= 4 ? (uint16_t)(stack[2] | (stack[3] << 8)) : 0;
            const uint16_t ret2 = stack.size() >= 6 ? (uint16_t)(stack[4] | (stack[5] << 8)) : 0;
            std::printf("raw change tick=%llu pc=%04X appended=\"%s\"\n",
                        (unsigned long long)emu.get_tick_count(),
                        emu.get_current_pc(),
                        raw.substr(split).c_str());
            std::printf("cpu sp=%04X hl=%04X de=%04X bc=%04X ret=%04X/%04X/%04X halted=%d iff1=%d iff2=%d\n",
                        cpu.sp, cpu.hl, cpu.de, cpu.bc,
                        ret0, ret1, ret2,
                        cpu.halted ? 1 : 0,
                        cpu.iff1 ? 1 : 0,
                        cpu.iff2 ? 1 : 0);
            std::printf("recent pcs:");
            for (uint16_t pc : recent_pcs)
                std::printf(" %04X", pc);
            std::printf("\n");
            if (emu.dbg_im2_ack_count != 0) {
                const uint32_t total = emu.dbg_im2_ack_count;
                const uint32_t start = total > 8 ? (total - 8) : 0;
                std::printf("recent im2 ack:");
                for (uint32_t idx = start; idx < total; ++idx) {
                    const uint32_t ring = idx & 0x7u;
                    std::printf(" [%u]=%02X@%04X",
                                idx,
                                emu.dbg_im2_ack_vectors[ring],
                                emu.dbg_im2_ack_pcs[ring]);
                }
                std::printf("\n");
            }
            prev_raw = raw;
        }
    }

    dump_state("idle", emu);
    dump_runtime("idle", emu, K, O);
    return 0;
}
