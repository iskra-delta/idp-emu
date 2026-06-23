// probe_bootload.cpp
//
// Headless verification that the PartOS boot ROM loads the split OS image
// byte-for-byte. It does NOT continue through the kernel: it lets the
// bootstrap decompress stage-1, redirects execution straight to the selected
// boot path (isolating the load path from the NVRAM/setup-window gate), runs
// until the ROM hands off to the low-page kernel entry at 0x0000, then
// compares RAM against the disk bytes.
//
// Layout the loader produces (start.s):
//   disk sector 0           -> BOOT_RECORD_BUF  (boot record scratch)
//   disk sectors 1..8       -> 0x0000           (micro-kernel)
//   disk sectors 9..72      -> 0xC000           (OS payload)
//
// One exception is expected after the copy completes: the ROM writes a tiny
// boot-hint block into the dead bytes at page-0+4..+7 before it jumps to the
// kernel. Those bytes are therefore excluded from the byte-for-byte compare.
//
// 2026-06-15   tstih
#include "partner_crt.hpp"
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace {
// Absolute addresses from partos/build/stage1.map + start.lst:
constexpr uint16_t PC_STAGE1_READY = 0x2003; // overlay disabled, about to call model_detect
constexpr uint16_t PC_BOOT_FD    = 0x2046; // boot_fd_path in start.s
constexpr uint16_t PC_BOOT_HD    = 0x2052; // boot_hd_path in start.s
constexpr uint16_t ADDR_MODEL       = 0xDE0E; // model byte
constexpr uint16_t ADDR_NV_CACHE    = 0xDE0F; // bios_nvram_cache

std::vector<uint8_t> read_file(const std::string &p) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    if (!f) { fprintf(stderr, "cannot open %s\n", p.c_str()); exit(2); }
    auto n = (size_t)f.tellg();
    f.seekg(0);
    std::vector<uint8_t> v(n);
    f.read(reinterpret_cast<char *>(v.data()), (std::streamsize)n);
    return v;
}

} // namespace

int main(int argc, char **argv) {
    std::string rom  = (argc > 1) ? argv[1] : "partos/bin/partos.rom";
    std::string mode = (argc > 2) ? argv[2] : "fd"; // "fd" or "hd"
    bool hd = (mode == "hd");
    std::string srcdisk  = hd ? "disks/hdd-dos.img" : "disks/fdd-dos.img";
    std::string testdisk = hd ? "/tmp/hdd-bootload-test.img"
                              : "/tmp/fdd-bootload-test.img";
    uint16_t redirect = hd ? PC_BOOT_HD : PC_BOOT_FD;
    const uint16_t pc_handoff = 0x0000; // ROM jp's here after the split load

    constexpr int SEC = 256;
    constexpr int UKSEC = 8, SVSEC = 64;        // 2 KB low + 16 KB high (partos.inc)
    constexpr int NPLANT = 1 + UKSEC + SVSEC;   // boot record + split OS image

    // Build a test disk: copy the real image, plant a deterministic pattern in
    // the full split reserved region, then restore the sector-0 boot signature
    // so the loader accepts the medium.
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
    static const std::vector<uint8_t> k_nvram = {
        0x00, 0x40, 0x80, 0x3F, 0x00, 0x00, 0x00, 0x02,
    };
    idp.write_debug_memory(ADDR_MODEL, {0x00});
    idp.write_debug_memory(ADDR_NV_CACHE, k_nvram);
    auto st = idp.capture_debug_cpu_state();
    st.sp = 0xBFFF;
    idp.apply_debug_cpu_state(st);
    idp.debug_set_pc(redirect);
    printf("[dbg] %s path: pc=%04X sp=%04X model=%02X\n", hd ? "HD" : "FD",
           idp.get_current_pc(), idp.capture_debug_cpu_state().sp,
           idp.peek_mem(ADDR_MODEL));

    // 3) Run until the ROM hands off to the low-page kernel entry.
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
        if (++guard > 120000000ULL) { // 72 sectors; floppy reads are slow
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
            const uint16_t addr = (uint16_t)(ram + i);
            if (addr >= 0x0004 && addr <= 0x0007)
                continue;               // ROM-owned boot-hint scratch
            uint8_t got = idp.peek_mem(addr);
            uint8_t exp = disk[diskoff + i];
            if (got != exp) { mism++; if (first < 0) first = diskoff + i; }
        }
    };
    check(0x0000, 1 * SEC, UKSEC * SEC);            // micro-kernel <- sectors 1..8
    check(0xC000, (1 + UKSEC) * SEC, SVSEC * SEC);  // services   <- sectors 9..72

    if (mism == 0) {
        printf("PASS: split OS image loaded exactly (2 KB @ 0x0000 + 16 KB @ 0xC000, "
               "%llu ticks)\n", (unsigned long long)idp.get_tick_count());
        printf("  0x0000=%02X (disk sec1[0]=%02X)  0xC000=%02X (disk sec9[0]=%02X)\n",
               idp.peek_mem(0x0000), disk[1 * SEC],
               idp.peek_mem(0xC000), disk[(1 + UKSEC) * SEC]);
        return 0;
    }
    printf("FAIL: %d byte mismatches, first at disk offset %d "
           "(sector %d, offset %d)\n", mism, first, first / 256, first % 256);
    return 1;
}
