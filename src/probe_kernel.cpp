// probe_kernel.cpp
//
// Functional smoke test for the PartOS *kernel* (not just the ROM). The kernel
// image is larger than the ROM's 8 KB OS-load window, so it cannot boot the
// normal way yet. Instead we drive it directly:
//
//   1. reset the emulator (this seeds the emulated RTC/NVRAM),
//   2. overlay the linked kernel.bin straight into RAM at its link base
//      (0x10000 - filesize == _CODE base == __sys_kernel),
//   3. run a 5-byte RAM stub `out (0x80),a ; jp __sys_page0_install` to disable
//      the ROM overlay and hand off through the real page-0 installer (B = model,
//      HL = continuation), which seeds __sys_nvram_cache from the NVRAM ports
//      and installs page 0 into both banks,
//   4. let the scheduler run (CTC VBL ticks drive the bootstrap thread).
//
// No ROM image is required, so this is independent of the ROM build. This is
// the staging ground for the 15 KB squeeze regression net; first milestone:
// does the direct-loaded kernel run init + device probe and settle, or derail?
//
// 2026-06-17   tstih

#include "partner.hpp"

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

constexpr uint16_t STUB_ADDR        = 0x2000; // RAM, above the ROM overlay window
constexpr uint64_t RUN_TICK_LIMIT   = 12'000'000ULL;
constexpr uint8_t  VBL_VECTOR       = 0x8E;   // CTC ch3 / VBL vector -> __thread_robin
constexpr uint64_t VBL_PERIOD       = 60000;  // ~realistic 60 Hz VBL cadence
uint8_t g_kernel_i                  = 0xFD;   // linked _IM2 page, filled from map

// The base `partner` model emits no VBL interrupt, and the GDP model gates it
// on I==0xFA (the BIOS IM2 page). The kernel sets I from the linked _IM2 page,
// so this test model follows the map instead of hardcoding one value. It
// injects the VBL vector
// (0x8E) on a periodic short hold once the kernel's IM2 table is installed,
// which is exactly what __sys_kernel waits on to dispatch the bootstrap thread.
class kernel_probe_partner : public partner {
public:
    void tick() override {
        if (get_cpu().i == g_kernel_i) {
            if ((get_tick_count() % VBL_PERIOD) == 0) vbl_hold_ = 64;
            if (vbl_hold_ > 0) --vbl_hold_;
        }
        partner::tick();
    }
    int get_external_im2_vector() const override {
        return (get_cpu().i == g_kernel_i && vbl_hold_ > 0) ? VBL_VECTOR : -1;
    }
private:
    int vbl_hold_ = 0;
};

static std::vector<uint8_t> read_file(const std::string &p)
{
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    if (!f) return {};
    auto n = (size_t)f.tellg();
    std::vector<uint8_t> v(n);
    f.seekg(0);
    f.read(reinterpret_cast<char *>(v.data()), (std::streamsize)n);
    return v;
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
    std::printf("FATAL: area %s not in map\n", area.c_str());
    std::exit(2);
}

// The sdldz80 map truncates symbol names to 9 chars and packs several per line.
// Parse every "ADDR  truncname" token into truncname -> sorted addresses, so we
// can resolve kernel symbols by their (9-char) name regardless of link shifts.
struct symbol_map {
    std::map<std::string, std::vector<uint16_t>> syms;
    explicit symbol_map(const std::string &path) {
        std::ifstream f(path);
        std::string line;
        const std::regex re("([0-9A-Fa-f]{8})  ([_A-Za-z.][_A-Za-z0-9$.]*)");
        while (std::getline(f, line)) {
            for (auto it = std::sregex_iterator(line.begin(), line.end(), re);
                 it != std::sregex_iterator(); ++it) {
                uint16_t a = (uint16_t)std::stoul((*it)[1].str(), nullptr, 16);
                syms[(*it)[2].str()].push_back(a);
            }
        }
        for (auto &kv : syms) { std::sort(kv.second.begin(), kv.second.end());
            kv.second.erase(std::unique(kv.second.begin(), kv.second.end()), kv.second.end()); }
    }
    // name is truncated to 9 chars to match the map; idx selects among duplicates.
    uint16_t at(const std::string &name, size_t idx = 0) const {
        auto it = syms.find(name.substr(0, 9));
        if (it == syms.end() || idx >= it->second.size()) {
            std::printf("FATAL: symbol '%s'[%zu] not in map\n", name.c_str(), idx);
            std::exit(2);
        }
        return it->second[idx];
    }
};

} // namespace

int main(int argc, char **argv)
{
    std::string kernel = (argc > 1) ? argv[1] : "partos/bin/kernel.bin";
    std::string disk   = (argc > 2) ? argv[2] : "";

    // CTest mode: with no args and a known source root, build the kernel and a
    // FAT12 fixture disk carrying a dummy /SHELL.XL, so the test is hermetic.
#ifdef IDP_SOURCE_ROOT
    if (argc <= 1) {
        const std::string root = IDP_SOURCE_ROOT;
        if (std::system(("make -C " + root + "/partos -s kernel").c_str()) != 0) {
            std::puts("FAIL: kernel build failed"); return 1;
        }
        disk = "/tmp/partos-kernel-boot-fixture.img";
        if (std::system(("python3 " + root + "/tools/mkdosdisk.py --shelldisk " + disk).c_str()) != 0) {
            std::puts("FAIL: fixture build failed"); return 1;
        }
        kernel = root + "/partos/bin/kernel.bin";
    }
#endif

    const auto kimg = read_file(kernel);
    if (kimg.empty()) { std::printf("FAIL: cannot read kernel %s\n", kernel.c_str()); return 1; }
    const uint16_t base = (uint16_t)(0x10000u - kimg.size());
    std::printf("kernel.bin = %zu bytes, link base = 0x%04X (= __sys_kernel)\n",
                kimg.size(), base);

    // resolve kernel symbols from the linker map (robust across link shifts).
    std::string mappath = kernel;
    auto pos = mappath.find("bin/kernel.bin");
    if (pos != std::string::npos) mappath.replace(pos, 14, "build/kernel.map");
    const uint16_t page0_install = (uint16_t)(read_area_addr(mappath, "_PAGE0") + 0x006B);
    g_kernel_i = (uint8_t)(read_area_addr(mappath, "_IM2") >> 8);
    symbol_map M(mappath);

    kernel_probe_partner emu;
    if (!disk.empty()) emu.load_disk(0, disk);
    emu.reset();

    // overlay the kernel image into RAM at its link base.
    emu.write_debug_memory(base, kimg);

    // stub: disable ROM overlay, then jump to the page-0 installer.
    const std::vector<uint8_t> stub = {
        0xD3, 0x80,                                   // out (0x80),a  -> rom_enabled=false
        0xC3, (uint8_t)(page0_install & 0xFF),        // jp __sys_page0_install
        (uint8_t)(page0_install >> 8),
    };
    emu.write_debug_memory(STUB_ADDR, stub);

    auto st = emu.capture_debug_cpu_state();
    st.af = 0x0000; st.bc = 0x0000;  // B = model 0 (CRT)
    st.de = 0x0000; st.hl = base;    // HL = continuation = __sys_kernel
    st.ix = st.iy = 0x0000;
    st.sp = 0xFFFF; st.halted = false;
    emu.apply_debug_cpu_state(st);
    emu.debug_set_pc(STUB_ADDR);
    const uint16_t SYS_NVRAM_CACHE  = M.at("__sys_nvram_cache");
    const uint16_t PROCESS_LOAD_IMG = M.at("_process_load_image", 2); // 3rd _process_*
    const uint16_t FAT_MOUNT        = M.at("_fat_mount", 1);          // [0]=_fat_mount_dev
    const uint16_t FAT_OPEN         = M.at("_fat_open");
    const uint16_t FAT_READ         = M.at("_fat_read");
    const uint16_t DEV_FIRST        = M.at("_dev_first");

    // phase 1: run the page-0 installer up to the kernel entry (== base), then
    // patch NVRAM so fd0 = PARTNER (18 spt / 256 B, matching the emulated disk)
    // and no hard disk (byte 2 = 0, skips slow SASI poll timeouts).
    bool at_entry = false;
    for (uint64_t i = 0; i < 2'000'000ULL; ++i) {
        emu.tick();
        if (emu.get_current_pc() == base) { at_entry = true; break; }
    }
    if (!at_entry) { std::puts("FAIL: never reached kernel entry"); return 1; }
    emu.write_debug_memory(SYS_NVRAM_CACHE + 1, {0x40}); // fd0 = type 1 (PARTNER)
    emu.write_debug_memory(SYS_NVRAM_CACHE + 2, {0x00}); // no hard disks
    std::puts("at kernel entry; NVRAM patched fd0=PARTNER, no HD");

    // phase 2: run the scheduler and record how far the boot path advances. Each
    // milestone is a distinct PC the kernel can only reach if the previous stage
    // worked, so the furthest milestone is a precise "boot depth" gauge. The dummy
    // /SHELL.XL is never executed: process_load_image is a stop marker, not run.
    const uint16_t KBOOT      = M.at("_kernel_bootstrap"); // scheduler dispatched a thread
    const uint16_t FAT_WORKER = (uint16_t)(M.at("_fat_init") - 0x68); // fat_worker$ ran
    const uint16_t FI         = M.at("_fat_init");         // FAT subsystem init
    const uint16_t FAT_HANDLE_MOUNT = M.at("fat_handle_mount");
    const uint16_t FAT_DEV_OPEN     = M.at("fat_dev_open");
    const uint16_t FD_READ          = M.at("fd_read");
    struct stage { const char *name; uint16_t pc; bool hit; };
    stage stages[] = {
        {"bootstrap_dispatched", KBOOT, false},
        {"fat_mount_called",     FAT_MOUNT, false},
        {"fat_init",             FI, false},
        {"worker_ran",           FAT_WORKER, false},
        {"mount_dispatched",     FAT_HANDLE_MOUNT, false},
        {"device_opened",        FAT_DEV_OPEN, false},
        {"block_read_issued",    FD_READ, false},
        {"fat_open_called",      FAT_OPEN, false},
        {"fat_read_called",      FAT_READ, false},
        {"loader_reached",       PROCESS_LOAD_IMG, false},
    };
    // worker dispatch markers (relative to _fat_init per fat.s .lst):
    const uint16_t W_DISPATCH = (uint16_t)(FI - 0x59); // fw_dispatch$ (got a request)
    const uint16_t W_WAIT     = (uint16_t)(FI - 0x5E); // call fat_wait_one$ (queue empty)
    const uint16_t W_MOUNT    = (uint16_t)(FI - 0x2A); // fw_mount$ (dispatch mount)
    bool w_dispatch=false, w_wait=false, w_mount=false;
    for (uint64_t i = 0; i < RUN_TICK_LIMIT; ++i) {
        emu.tick();
        const uint16_t pc = emu.get_current_pc();
        for (auto &s : stages) if (pc == s.pc) s.hit = true;
        if (pc == W_DISPATCH) w_dispatch = true;
        if (pc == W_WAIT)     w_wait = true;
        if (pc == W_MOUNT)    w_mount = true;
        if (pc == PROCESS_LOAD_IMG) break;
    }
    std::printf("worker: got_request=%d empty_wait=%d dispatch_mount=%d\n",
                w_dispatch, w_wait, w_mount);

    // device chain the kernel enumerated from NVRAM (exercises dev/drv/probe).
    std::string devs;
    {
        auto hb = emu.read_debug_memory(DEV_FIRST, 2);
        uint16_t dev = (uint16_t)hb[0] | ((uint16_t)hb[1] << 8);
        for (int i = 0; i < 16 && dev; ++i) {
            auto raw = emu.read_debug_memory(dev, 30);
            std::string nm;
            for (int j = 0; j < 8 && raw[2 + j]; ++j) nm.push_back((char)raw[2 + j]);
            if (!devs.empty()) devs += ",";
            devs += nm.empty() ? "?" : nm;
            dev = (uint16_t)raw[0] | ((uint16_t)raw[1] << 8);
        }
    }
    std::printf("devices=[%s]\n", devs.c_str());
    int depth = 0;
    for (auto &s : stages) { std::printf("  [%c] %s\n", s.hit ? 'x' : ' ', s.name);
                             if (s.hit) ++depth; }

    const bool fat_complete = stages[9].hit;     // loader_reached
    const bool boot_floor   = stages[1].hit &&   // fat_mount_called
                              devs.find("fd0") != std::string::npos;
    if (fat_complete) { std::puts("PASS: full FAT mount+open+read path reached"); return 0; }
    if (boot_floor)   { std::printf("PARTIAL: boot floor OK (depth %d/10); FAT completion pending\n", depth); return 0; }
    std::puts("FAIL: regressed below boot floor (device probe / scheduler / mount submit)");
    return 1;
}
