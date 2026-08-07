// probe_hang_trace.cpp
#include <memory>
//
// Diagnose the PartOS OS layout-fragility hang: when os.sys grows past its
// (hidden) ~15055-byte ceiling, boot hangs before the banner. This probe boots
// the current ROM/disks, and if the banner never appears it reports WHERE the
// CPU is stuck: the hot PC range over the final window, plus SP / I / bank.
// Run against a known-good build and a known-bad (padded) build and diff.
//
// usage: idp-hang-trace [source-root]

#include "partner_gdp.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

namespace {

constexpr uint64_t BOOT_TICK_LIMIT = 120'000'000ULL;

bool banner_present(const scn2674_t &avdc)
{
    // row 0 base pointer, same scheme the golden tests use.
    const uint16_t raw =
        (uint16_t)(avdc.vram[0] | ((uint16_t)avdc.vram[1] << 8));
    const uint16_t base = (uint16_t)(raw & 0x3FFFu);
    std::string line;
    for (int col = 0; col < 40; ++col) {
        const uint8_t ch = avdc.vram[(base + col) & 0x3FFFu];
        line.push_back((ch >= 0x20 && ch < 0x7F) ? (char)ch : ' ');
    }
    return line.find("PARTOS") != std::string::npos;
}

} // namespace

int main(int argc, char **argv)
{
    std::string root = "/home/tstih/data/iskra-delta/idp-emu";
    if (argc > 1)
        root = argv[1];

    const std::string rom_path = root + "/partos/bin/partos.rom";
    const std::string fdd_path = root + "/disks/fdd-dos.img";
    const std::string hdd_path = root + "/disks/hdd-dos.img";

    std::unique_ptr<partner_gdp> emup;
    if (const char *nv = std::getenv("IDP_NVRAM"))
        emup = std::make_unique<partner_gdp>(terminal_profile::vt100_ansi, std::string(nv));
    else
        emup = std::make_unique<partner_gdp>(terminal_profile::vt100_ansi);
    partner_gdp &emu = *emup;
    emu.load_rom(rom_path);
    emu.load_disk(0, fdd_path);
    emu.load_hdd(hdd_path);
    emu.reset();

    bool saw_banner = false;
    uint64_t banner_tick = 0;

    // Track last tick at which the PC reached a NEW maximum in the OS payload
    // window (0xC000..0xFFFF) -- a crude "forward progress" signal.
    uint16_t max_os_pc = 0;
    uint64_t last_progress_tick = 0;

    // Rolling window of recent PCs to identify the stuck loop.
    constexpr uint64_t WINDOW = 200'000ULL;
    std::map<uint16_t, uint64_t> window_hist;
    uint64_t window_start = 0;

    // Ring buffer of the last distinct OS-code PCs (0xC000..0xFB00), so we can
    // see the last real OS instructions executed before the system idles.
    constexpr size_t RING = 48;
    std::vector<uint16_t> os_ring;
    uint16_t last_os_pc = 0;

    // Windowed first-visit trace (env TRACE_T0/TRACE_T1): the ordered set of
    // distinct OS-code PCs first seen in [T0,T1]. Diff good vs bad to find the
    // divergence in the shell-start path.
    uint64_t T0 = 0, T1 = 0;
    if (const char *s = std::getenv("TRACE_T0")) T0 = std::strtoull(s, nullptr, 0);
    if (const char *s = std::getenv("TRACE_T1")) T1 = std::strtoull(s, nullptr, 0);
    std::vector<uint16_t> win_trace;
    std::map<uint16_t, bool> win_seen;

    for (uint64_t i = 0; i < BOOT_TICK_LIMIT; ++i) {
        emu.tick();
        const uint16_t pc = emu.get_current_pc();

        if (T1 && i >= T0 && i <= T1 && pc >= 0xC000 && pc < 0xFB00 &&
            !win_seen[pc]) {
            win_seen[pc] = true;
            win_trace.push_back(pc);
        }

        if (pc >= 0xC000 && pc < 0xFB00 && pc != last_os_pc) {
            last_os_pc = pc;
            os_ring.push_back(pc);
            if (os_ring.size() > RING)
                os_ring.erase(os_ring.begin());
        }

        if (pc >= 0xC000 && pc > max_os_pc) {
            max_os_pc = pc;
            last_progress_tick = i;
        }

        if (i >= window_start + WINDOW) {
            window_hist.clear();
            window_start = i;
        }
        window_hist[pc]++;

        if ((i & 0x3FFFu) == 0) {
            if (banner_present(emu.get_avdc())) {
                saw_banner = true;
                banner_tick = i;
                break;
            }
        }
    }

    // Optional: after boot, type KEYS (\\n allowed) with KEY_TICKS spacing, then
    // report where execution ends up (to pinpoint a mid-typing keyboard stall).
    auto count_prompts = [&]() {
        const scn2674_t &a = emu.get_avdc();
        int n = 0;
        for (int row = 0; row < 26; ++row) {
            const uint16_t pp = (uint16_t)((row*2)&0x3FFFu);
            const uint16_t raw = (uint16_t)(a.vram[pp]|((uint16_t)a.vram[(pp+1)&0x3FFFu]<<8));
            const uint16_t base = (uint16_t)(raw & 0x3FFFu);
            char c0=(char)a.vram[base&0x3FFFu];
            if (c0=='h'||c0=='f')
                for(int col=0;col<40;col++) if(a.vram[(base+col)&0x3FFFu]=='>'){n++;break;}
        }
        return n;
    };
    // wait until the number of prompt rows reaches at least `target`
    auto wait_prompt_count = [&](int target, uint64_t limit) {
        for (uint64_t k = 0; k < limit; ++k) {
            emu.tick();
            if ((k & 0x1FFFu) == 0 && count_prompts() >= target) return;
        }
    };
    // exactly the golden tests' wait_for_command_return: a row containing the
    // command text with a prompt row strictly below it.
    auto row_text = [&](int row) {
        const scn2674_t &a = emu.get_avdc();
        const uint16_t pp = (uint16_t)((row*2)&0x3FFFu);
        const uint16_t raw = (uint16_t)(a.vram[pp]|((uint16_t)a.vram[(pp+1)&0x3FFFu]<<8));
        const uint16_t base = (uint16_t)(raw & 0x3FFFu);
        std::string s;
        for (int col=0;col<40;col++){uint8_t ch=a.vram[(base+col)&0x3FFFu];s.push_back((ch>=0x20&&ch<0x7F)?(char)ch:'.');}
        while(!s.empty()&&s.back()==' ')s.pop_back();
        return s;
    };
    auto wait_cmd_return = [&](const std::string &cmd, uint64_t limit) {
        for (uint64_t k = 0; k < limit; ++k) {
            emu.tick();
            if ((k & 0x1FFFu) != 0) continue;
            int cmd_row=-1, prompt_row=-1;
            for (int row=0;row<26;row++){
                std::string s=row_text(row);
                if (cmd_row<0 && s.find(cmd)!=std::string::npos) cmd_row=row;
                if (s.size()>=4 && (s[0]=='h'||s[0]=='f') && s.find('>')!=std::string::npos) prompt_row=row;
            }
            if (cmd_row>=0 && prompt_row>cmd_row) return;
        }
    };

    if (const char *keys = std::getenv("KEYS")) {
        if (saw_banner) {
            wait_prompt_count(1, 40'000'000ULL);  // wait for the first shell prompt
            const uint64_t KT = 5000;
            std::string cur;   // command text accumulated since last CR
            for (const char *p = keys; *p; ++p) {
                char ch = *p;
                if (ch == '\\' && p[1] == 'n') { ch = '\r'; ++p; }
                emu.key_input((uint8_t)ch);
                for (uint64_t k = 0; k < KT; ++k) emu.tick();
                if (ch == '\r') { wait_cmd_return(cur, 20'000'000ULL); cur.clear(); }
                else cur.push_back(ch);
            }
            // let it settle, capture last OS PCs + full-PC histogram
            os_ring.clear(); last_os_pc = 0;
            std::map<uint16_t, uint64_t> hh;
            for (uint64_t k = 0; k < 8'000'000ULL; ++k) {
                emu.tick();
                const uint16_t pc = emu.get_current_pc();
                if (k >= 8'000'000ULL - 400'000ULL) hh[pc]++;
                if (pc >= 0xC000 && pc < 0xFB00 && pc != last_os_pc) {
                    last_os_pc = pc;
                    os_ring.push_back(pc);
                    if (os_ring.size() > RING) os_ring.erase(os_ring.begin());
                }
            }
            const auto st2 = emu.capture_debug_cpu_state();
            std::printf("[settle] halted=%d PC=0x%04X SP=0x%04X\n",
                        st2.halted?1:0, emu.get_current_pc(), st2.sp);
            std::vector<std::pair<uint16_t,uint64_t>> hv(hh.begin(),hh.end());
            std::sort(hv.begin(),hv.end(),[](auto&a,auto&b){return a.second>b.second;});
            std::printf("[settle hot PCs]:");
            for (size_t k=0;k<hv.size()&&k<12;k++)
                std::printf(" 0x%04X(%llu)",hv[k].first,(unsigned long long)hv[k].second);
            std::printf("\n");
            std::printf("[KEYS] typed %zu chars\n", std::string(keys).size());
            {
                const scn2674_t &a = emu.get_avdc();
                for (int row = 0; row < 6; ++row) {
                    const uint16_t p = (uint16_t)((row * 2) & 0x3FFFu);
                    const uint16_t raw = (uint16_t)(a.vram[p] | ((uint16_t)a.vram[(p+1)&0x3FFFu]<<8));
                    const uint16_t base = (uint16_t)(raw & 0x3FFFu);
                    std::string line;
                    for (int col = 0; col < 40; ++col) {
                        uint8_t ch = a.vram[(base+col)&0x3FFFu];
                        line.push_back((ch>=0x20&&ch<0x7F)?(char)ch:'.');
                    }
                    while(!line.empty()&&line.back()==' ')line.pop_back();
                    std::printf("  row%d \"%s\"\n", row, line.c_str());
                }
            }
            auto rd = [&](uint16_t a){ return emu.read_debug_memory(a,1)[0]; };
            std::printf("[SIO st] RXSZ=%u RXHEAD=%u RXTAIL=%u RXCOUNT=%u "
                        "RDLEFT=%u RDBUF=0x%02X%02X\n",
                        rd(0x1080), rd(0x1082), rd(0x1083), rd(0x1084),
                        rd(0x108A) | (rd(0x108B)<<8), rd(0x1089), rd(0x1088));
            std::printf("[ir] ir_refcnt(0xFB71)=%u ir_armed(0xFB72)=%u\n",
                        rd(0xFB71), rd(0xFB72));
            std::printf("[kbd] console_kbd_char(0x1264)=0x%02X\n", rd(0x1264));
            std::printf("[ISR dbg] nulldisc(0x1110)=%u overrun(0x1111)=%u\n",
                        rd(0x1110), rd(0x1111));
            std::printf("[ring 0x1000]:");
            auto ring = emu.read_debug_memory(0x1000, 32);
            for (int k=0;k<32;k++) std::printf(" %02X", ring[k]);
            std::printf("\n");
            std::printf("[last OS PCs]:");
            for (size_t k=0;k<os_ring.size();k++)
                std::printf("%s0x%04X",(k%8==0?"\n  ":" "),os_ring[k]);
            std::printf("\n");
        }
    }

    std::printf("=== hang-trace ===\n");
    std::printf("banner: %s", saw_banner ? "YES" : "NO");
    if (saw_banner)
        std::printf(" @tick %llu", (unsigned long long)banner_tick);
    std::printf("\n");
    std::printf("max OS PC reached: 0x%04X (last advanced @tick %llu)\n",
                max_os_pc, (unsigned long long)last_progress_tick);
    const auto st = emu.capture_debug_cpu_state();
    std::printf("final PC=0x%04X SP=0x%04X I=0x%02X iff1=%d halted=%d bank=%u romen=%d\n",
                emu.get_current_pc(), st.sp, st.i, st.iff1 ? 1 : 0,
                st.halted ? 1 : 0,
                (unsigned)emu.get_ram_bank(), emu.is_rom_enabled() ? 1 : 0);

    // Report the hot PCs in the final window (the stuck loop).
    std::vector<std::pair<uint16_t, uint64_t>> hot(window_hist.begin(),
                                                    window_hist.end());
    std::sort(hot.begin(), hot.end(),
              [](auto &a, auto &b) { return a.second > b.second; });
    std::printf("hot PCs in final %llu-tick window:\n",
                (unsigned long long)WINDOW);
    for (size_t k = 0; k < hot.size() && k < 16; ++k)
        std::printf("  0x%04X : %llu\n", hot[k].first,
                    (unsigned long long)hot[k].second);

    std::printf("last %zu distinct OS-code PCs (0xC000..0xFB00) executed:\n",
                os_ring.size());
    for (size_t k = 0; k < os_ring.size(); ++k)
        std::printf("  %s0x%04X", (k % 8 == 0 ? "\n  " : " "), os_ring[k]);
    std::printf("\n");

    if (T1) {
        std::printf("windowed first-visit OS PCs [%llu,%llu] (%zu distinct):",
                    (unsigned long long)T0, (unsigned long long)T1,
                    win_trace.size());
        for (size_t k = 0; k < win_trace.size(); ++k)
            std::printf("%s0x%04X", (k % 8 == 0 ? "\n  " : " "), win_trace[k]);
        std::printf("\n");
    }

    return saw_banner ? 0 : 2;
}
