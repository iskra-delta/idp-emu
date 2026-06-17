// probe_bootload.cpp
//
// Headless verification that the PartOS boot ROM loads the OS image byte-for-
// byte. It does NOT boot into an OS: it lets the bootstrap decompress stage-1,
// redirects execution straight to boot_main$ (isolating the load path from the
// NVRAM/setup-window gate), runs until the loader hands off at the linked
// __sys_page0_install entry, then compares RAM against the disk bytes.
//
// Layout the loader produces (start.s):
//   disk sector 0      -> 0xDF00..0xDFFF   (boot record)
//   disk sectors 1..32 -> 0xE000..0xFFFF   (8 KB OS image)
//
// 2026-06-15   tstih
#include "partner_crt.hpp"
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <regex>
#include <string>
#include <vector>

namespace {
// Absolute addresses from partos/build/stage1.map + start.lst:
constexpr uint16_t PC_START_MAIN = 0x2000; // stage-1 entry (decompressed)
constexpr uint16_t PC_STAGE1_READY = 0x2003; // overlay disabled, about to call model_detect
constexpr uint16_t PC_FD_PATH    = 0x2046; // boot_fd_path in start.s
constexpr uint16_t PC_HD_PATH    = 0x2052; // boot_hd_path in start.s
constexpr uint16_t ADDR_MODEL    = 0xDE0C; // model byte (_SYSVARS @ 0xDE00)
constexpr uint16_t ADDR_NV_VALID = 0xDE15; // bios_nvram_valid

std::vector<uint8_t> read_file(const std::string &p) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    if (!f) { fprintf(stderr, "cannot open %s\n", p.c_str()); exit(2); }
    auto n = (size_t)f.tellg();
    f.seekg(0);
    std::vector<uint8_t> v(n);
    f.read(reinterpret_cast<char *>(v.data()), (std::streamsize)n);
    return v;
}

uint16_t read_area_addr(const std::string &path, const std::string &area) {
    std::ifstream f(path);
    if (!f) {
        fprintf(stderr, "cannot open map %s\n", path.c_str());
        exit(2);
    }
    const std::regex re("^" + area + R"(\s+([0-9A-Fa-f]{8})\s+)");
    std::string line;
    std::smatch m;
    while (std::getline(f, line)) {
        if (std::regex_search(line, m, re))
            return (uint16_t)std::stoul(m[1].str(), nullptr, 16);
    }
    fprintf(stderr, "area %s not found in %s\n", area.c_str(), path.c_str());
    exit(2);
}
} // namespace

int main(int argc, char **argv) {
    std::string rom  = (argc > 1) ? argv[1] : "partos/bin/partos.rom";
    std::string mode = (argc > 2) ? argv[2] : "fd"; // "fd" or "hd"
    std::string kmap = "partos/build/kernel.map";
    bool hd = (mode == "hd");
    std::string srcdisk  = hd ? "disks/hdd-dos.img" : "disks/fdd-dos.img";
    std::string testdisk = hd ? "/tmp/hdd-bootload-test.img"
                              : "/tmp/fdd-bootload-test.img";
    uint16_t redirect = hd ? PC_HD_PATH : PC_FD_PATH;
    const uint16_t pc_handoff = (uint16_t)(read_area_addr(kmap, "_PAGE0") + 0x006B);

    constexpr int SEC = 256;
    constexpr int NPLANT = 33; // boot record + 32 OS sectors

    // Build a test disk: copy the real image, plant a deterministic pattern in
    // the first 33 sectors, then restore the sector-0 boot signature so the
    // loader accepts the medium.
    auto disk = read_file(srcdisk);
    if (disk.size() < (size_t)NPLANT * SEC) {
        fprintf(stderr, "disk too small\n"); return 2;
    }
    for (int s = 0; s < NPLANT; s++)
        for (int o = 0; o < SEC; o++)
            disk[s * SEC + o] = (uint8_t)(s * 37 + o * 5 + 0x21);
    disk[0 * SEC + 254] = 0x55; // boot signature (little-endian 0xAA55)
    disk[0 * SEC + 255] = 0xAA;
    {
        std::ofstream o(testdisk, std::ios::binary);
        o.write(reinterpret_cast<char *>(disk.data()), (std::streamsize)disk.size());
    }

    partner_crt idp(terminal_profile::vt52);
    idp.load_rom(rom);
    if (hd) idp.load_hdd(testdisk);
    else    idp.load_disk(0, testdisk);
    idp.reset();

    // 1) Let the bootstrap inflate stage-1 and execute the initial ROM-off
    //    handoff inside start_main. We redirect only after the overlay has
    //    been disabled by the live stage-1 code.
    uint64_t guard = 0;
    while (idp.is_rom_enabled() || idp.get_current_pc() != PC_STAGE1_READY) {
        idp.tick();
        if (++guard > 50000000ULL) {
            printf("FAIL: never reached ready stage-1 state (pc=%04X rom=%d)\n",
                   idp.get_current_pc(), idp.is_rom_enabled() ? 1 : 0);
            return 1;
        }
    }

    // 2) Isolate the load path: force the plain-text print route, give the
    //    loader a stack, and jump straight to boot_main$.
    idp.write_debug_memory(ADDR_MODEL, {0x00});
    idp.write_debug_memory(ADDR_NV_VALID, {0x00});
    auto st = idp.capture_debug_cpu_state();
    st.sp = 0xBFFF;
    idp.apply_debug_cpu_state(st);
    idp.debug_set_pc(redirect);
    printf("[dbg] %s path: pc=%04X sp=%04X model=%02X\n", hd ? "HD" : "FD",
           idp.get_current_pc(), idp.capture_debug_cpu_state().sp,
           idp.peek_mem(ADDR_MODEL));

    // 3) Run until the loader hands off to the kernel installer.
    guard = 0;
    uint16_t lastpc = 0xFFFF;
    std::vector<uint16_t> trail_first;
    while (idp.get_current_pc() != pc_handoff) {
        idp.tick();
        uint16_t pc = idp.get_current_pc();
        if (pc != lastpc) {
            lastpc = pc;
            if ((int)trail_first.size() < 90) trail_first.push_back(pc);
        }
        if (++guard > 8000000ULL) {
            printf("FAIL: loader never reached handoff (pc=%04X)\n", pc);
            printf("[trace] first distinct PCs from redirect:\n");
            for (size_t k = 0; k < trail_first.size(); k++)
                printf(" %04X%s", trail_first[k], ((k % 12) == 11) ? "\n" : "");
            printf("\n");
            return 1;
        }
    }

    // 4) Compare loaded RAM to the disk bytes.
    int mism = 0, first = -1;
    auto check = [&](uint16_t ram, int diskoff, int n) {
        for (int i = 0; i < n; i++) {
            uint8_t got = idp.peek_mem((uint16_t)(ram + i));
            uint8_t exp = disk[diskoff + i];
            if (got != exp) { mism++; if (first < 0) first = diskoff + i; }
        }
    };
    check(0xDF00, 0, 256);       // boot record  <- sector 0
    check(0xE000, 256, 8192);    // 8 KB OS image <- sectors 1..32

    if (mism == 0) {
        printf("PASS: boot record (0xDF00) and 8 KB OS image (0xE000-0xFFFF) "
               "match disk bytes exactly (%llu ticks)\n",
               (unsigned long long)idp.get_tick_count());
        // spot values for visibility
        printf("  0xE000=%02X (disk sec1[0]=%02X)  0xFFFF=%02X (disk sec32[255]=%02X)\n",
               idp.peek_mem(0xE000), disk[256],
               idp.peek_mem(0xFFFF), disk[256 + 8191]);
        return 0;
    }
    printf("FAIL: %d byte mismatches, first at disk offset %d "
           "(sector %d, offset %d)\n", mism, first, first / 256, first % 256);
    return 1;
}
