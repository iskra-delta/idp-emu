// probe_shell_regression.cpp
//
// PartOS shell regression pack.
//
// Boots straight to the PartOS shell prompt (same staged ROM->HD handoff the
// other shell probes use) and then drives a scripted sequence of shell
// commands as *real injected keypresses*. After each command it:
//
//   - waits for the shell prompt to return, up to a per-step tick budget
//     (a budget overrun is reported as a FREEZE/TIMEOUT failure)
//   - captures the serial-output delta the command produced
//   - asserts that every "expect" substring is present and every "forbid"
//     substring is absent (the universal PartOS error marker is '?')
//
// The script gives broad coverage of the file/dir command surface:
//   ls, echo, mkdir, cd, cp, mv, run-a-copied-program-locally, rename,
//   CWD-only command lookup (a negative test), slash-path execution from a
//   parent dir, del and rmdir, plus a final listing that proves teardown.
//
// Usage:
//   idp-shell-regression [SOURCE_ROOT]
// Env:
//   IDP_SKIP_BUILD=1   reuse existing ROM/sys/disk (do not rebuild)
//   IDP_RECORD=1       print every command delta and never fail the run
//                      (useful for eyeballing / refreshing expectations)
//   IDP_KEY_TICKS=N    ticks to run after each injected key   (default 4000)
//   IDP_STEP_BUDGET=N  max ticks to wait for a prompt return  (default 35e6)
//   IDP_STRESS_ROUNDS=N
//                      append N fast filesystem churn rounds:
//                      mkdir/cd/ls/cp/run/mv/run-path/del/rmdir
//
// Exit code is the number of failed steps (0 == all passed), except in
// IDP_RECORD mode which always returns 0.
//
// 2026-07-01   tstih

#include "partner_crt.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

struct regression_partner_crt : partner_crt {
    using partner_crt::partner_crt;
    using partner::cpu;
    using partner::sio;
};

constexpr uint16_t PC_STAGE1_READY   = 0x2003;
constexpr uint16_t PC_BOOT_HD        = 0x2052;
constexpr uint16_t ADDR_MODEL        = 0xDE0E;
constexpr uint16_t ADDR_NVRAM_CACHE  = 0xDE0F;
constexpr uint64_t BOOT_TICK_LIMIT   = 60'000'000ULL;
constexpr uint64_t DEFAULT_KEY_TICKS = 4000ULL;
constexpr uint64_t DEFAULT_STEP_BUDGET = 35'000'000ULL;
constexpr uint64_t POLL_INTERVAL     = 4096ULL;

struct step {
    std::string cmd;                    // command line, no trailing CR
    std::vector<std::string> expect;    // substrings that MUST appear
    std::vector<std::string> forbid;    // substrings that must NOT appear
    std::string note;                   // human-readable intent
};

static uint64_t env_u64_or(const char *name, uint64_t fallback)
{
    const char *s = std::getenv(name);

    if (s == nullptr || *s == '\0')
        return fallback;
    const unsigned long long parsed = std::strtoull(s, nullptr, 10);
    return parsed == 0ULL ? fallback : (uint64_t)parsed;
}

static uint64_t env_u64_allow_zero_or(const char *name, uint64_t fallback)
{
    const char *s = std::getenv(name);

    if (s == nullptr || *s == '\0')
        return fallback;
    char *end = nullptr;
    const unsigned long long parsed = std::strtoull(s, &end, 10);
    return (end != s && *end == '\0') ? (uint64_t)parsed : fallback;
}

static bool contains(const std::string &hay, const std::string &needle)
{
    return hay.find(needle) != std::string::npos;
}

// The shell prompt is a whole line ending in "> " whose prefix is the device
// alias and cwd, e.g. "hd0> " or "hd0/tdir> " -- crucially it contains no
// internal spaces. That distinguishes it from a directory-listing row such as
// "d  <dir>  ./" whose "<dir> " token also ends in "> " and would otherwise
// false-trigger a mid-command "prompt returned".
static bool text_ends_at_prompt(const std::string &text)
{
    if (text.size() < 2 || text.compare(text.size() - 2, 2, "> ") != 0)
        return false;
    const size_t nl = text.rfind('\n');
    const size_t line_start = (nl == std::string::npos) ? 0 : nl + 1;
    // The prompt core is the last line without its trailing "> ".
    for (size_t i = line_start; i + 2 < text.size(); ++i) {
        if (text[i] == ' ')
            return false;
    }
    return true;
}

static bool prompt_returned_after(const partner_crt &emu, size_t raw_baseline)
{
    const std::string raw = emu.dump_raw_serial_text();
    return raw.size() > raw_baseline && text_ends_at_prompt(raw);
}

static bool shell_prompt_seen(const partner_crt &emu)
{
    const std::string raw = emu.dump_raw_serial_text();
    if (contains(raw, "PARTOS shell") && contains(raw, "> "))
        return true;
    const std::string term = emu.dump_terminal_text();
    return contains(term, "PARTOS shell") && contains(term, "> ");
}

static bool debug_timeout_enabled()
{
    static const bool enabled = [] {
        const char *s = std::getenv("IDP_DEBUG_TIMEOUT");
        return s != nullptr && s[0] != '\0' && s[0] != '0';
    }();
    return enabled;
}

// Escape control bytes so the captured delta prints on a single readable line.
static std::string visualize(const std::string &s)
{
    std::string out;
    for (char c : s) {
        const unsigned char u = (unsigned char)c;
        if (u == '\r')
            out += "\\r";
        else if (u == '\n')
            out += "\\n";
        else if (u < 0x20 || u >= 0x7F)
            out += '.';
        else
            out.push_back(c);
    }
    return out;
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

static std::string numbered_name(const char *prefix, size_t n)
{
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%s%02zu", prefix, n);
    return std::string(buf);
}

static std::vector<std::string> root_listing_markers()
{
    return {
        "shell.com",
        "mkdir.com",
        "rmdir.com",
        "clear.com",
        "echo.com",
        "help.com",
    };
}

static std::vector<step> make_smoke_script()
{
    return {
        {"ls",
         {"shell", "echo", "mkdir"}, {"?"},
         "root listing shows the shipped commands"},

        {"echo hello world",
         {"hello world"}, {"?"},
         "echo passes its arguments through"},

        {"mkdir tdir",
         {}, {"?", "usage"},
         "create a working directory"},

        {"ls",
         {"tdir"}, {"?"},
         "new directory appears in the root listing"},

        {"cd tdir",
         {}, {"?"},
         "enter the working directory"},

        {"cp /ls.com dls.com",
         {}, {"?", "usage"},
         "copy a program into the current directory"},

        {"ls",
         {"dls.com"}, {"?"},
         "copied program is present in the cwd"},

        {"dls",
         {"dls.com"}, {"?"},
         "run the copied program locally (it lists the cwd)"},

        {"mv dls.com ren.com",
         {}, {"?", "usage"},
         "rename the program to a different name"},

        {"ls",
         {"ren.com"}, {"?", "dls.com"},
         "rename took effect (old name gone, new name present)"},

        {"ren",
         {"ren.com"}, {"?"},
         "run the renamed program locally"},

        {"ren /",
         {"shell.com"}, {"?"},
         "run the renamed program locally while listing root"},

        // A not-found command reports the '?' error and does not execute.
        {"dls",
         {"?"}, {"<dir>", "ren.com"},
         "old name no longer resolves in the cwd (reports '?')"},

        {"cd ..",
         {}, {"?"},
         "go back up to the root directory"},

        {"ren",
         {"?"}, {"<dir>", "shell.com"},
         "cwd-only lookup: subdir program does NOT run from root (reports '?')"},

        {"tdir/ren",
         {"shell.com"}, {"?"},
         "slash-path executes the subdir program from root (lists cwd=root)"},

        {"tdir/dls",
         {"?"}, {"<dir>", "shell.com"},
         "slash-path to the deleted name does not execute (reports '?')"},

        {"del tdir/ren.com",
         {}, {"?", "usage"},
         "delete the program file via a path"},

        {"rmdir tdir",
         {}, {"?", "usage"},
         "remove the now-empty working directory"},

        {"ls",
         {"shell"}, {"?", "tdir"},
         "teardown complete: directory is gone"},
    };
}

static void append_stress_rounds(std::vector<step> &script, size_t rounds)
{
    const std::vector<std::string> root_markers = root_listing_markers();

    for (size_t i = 0; i < rounds; ++i) {
        const std::string dir = numbered_name("s", i);
        const std::string prompt_root = "hd0> ";
        const std::string prompt_dir = "hd0/" + dir + "> ";
        const std::string label = "stress round " + std::to_string(i + 1) + ": ";

        script.push_back({
            "mkdir " + dir,
            {prompt_root}, {"?", "usage"},
            label + "create directory",
        });

        script.push_back({
            "cd " + dir,
            {prompt_dir}, {"?"},
            label + "enter directory and verify prompt cwd",
        });

        script.push_back({
            "ls",
            {prompt_dir}, root_markers,
            label + "empty cwd listing must not show root",
        });

        script.push_back({
            "cp /ls.com re.com",
            {prompt_dir}, {"?", "usage"},
            label + "copy ls into cwd as re.com",
        });

        script.push_back({
            "ls",
            {"re.com", prompt_dir}, {"?", "shell.com", "mkdir.com"},
            label + "cwd listing shows copied command only",
        });

        script.push_back({
            "re",
            {"re.com", prompt_dir}, {"?", "shell.com", "mkdir.com"},
            label + "run copied command from cwd",
        });

        script.push_back({
            "re /",
            {"shell.com", prompt_dir}, {"?", "re.com"},
            label + "copied command honors absolute path argument",
        });

        script.push_back({
            "mv re.com rn.com",
            {prompt_dir}, {"?", "usage"},
            label + "rename copied command",
        });

        script.push_back({
            "ls",
            {"rn.com", prompt_dir}, {"?", "re.com", "shell.com", "mkdir.com"},
            label + "renamed command appears and old name is gone",
        });

        script.push_back({
            "rn",
            {"rn.com", prompt_dir}, {"?", "shell.com", "mkdir.com"},
            label + "run renamed command from cwd",
        });

        script.push_back({
            "cd ..",
            {prompt_root}, {"?"},
            label + "return to root",
        });

        script.push_back({
            dir + "/rn",
            {"shell.com", prompt_root}, {"?", "rn.com"},
            label + "run copied command by slash path from root",
        });

        script.push_back({
            "del " + dir + "/rn.com",
            {prompt_root}, {"?", "usage"},
            label + "delete copied command through path",
        });

        script.push_back({
            "rmdir " + dir,
            {prompt_root}, {"?", "usage"},
            label + "remove directory",
        });

        script.push_back({
            "ls",
            {"shell.com", prompt_root}, {"?", dir},
            label + "root listing no longer contains removed directory",
        });
    }
}

static bool boot_to_shell(partner_crt &emu)
{
    uint64_t guard = 0;
    while (emu.is_rom_enabled() || emu.get_current_pc() != PC_STAGE1_READY) {
        emu.tick();
        if (++guard > 50'000'000ULL) {
            std::printf("FAIL: never reached stage-1 ready state (pc=%04X rom=%d)\n",
                        emu.get_current_pc(), emu.is_rom_enabled() ? 1 : 0);
            return false;
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

    for (guard = 0; guard < BOOT_TICK_LIMIT; ++guard) {
        emu.tick();
        if ((guard & 0x3FFu) == 0 && shell_prompt_seen(emu))
            return true;
    }
    std::printf("FAIL: never reached shell prompt (pc=%04X tick=%llu)\n",
                emu.get_current_pc(),
                (unsigned long long)emu.get_tick_count());
    std::printf("terminal:\n%s\n", emu.dump_terminal_text().c_str());
    return false;
}

struct step_result {
    bool pass = false;
    bool timeout = false;
    uint64_t ticks = 0;
    std::string delta;
    std::vector<std::string> missing;    // expected but absent
    std::vector<std::string> present;    // forbidden but present
};

static void dump_timeout_state(const regression_partner_crt &emu)
{
    const auto cpu = emu.capture_debug_cpu_state();
    const auto &ch = emu.sio.chn[Z80SIO_CHANNEL_A];
    const auto kvars = emu.read_debug_memory(0xFB71, 2);
    const auto threads = emu.read_debug_memory(0xFB99, 10);
    const uint8_t ir_ref = kvars.size() >= 1 ? kvars[0] : 0xFF;
    const uint8_t ir_armed = kvars.size() >= 2 ? kvars[1] : 0xFF;
    const auto rd16_at = [](const std::vector<uint8_t> &v, size_t off) -> uint16_t {
        return v.size() >= off + 2
            ? (uint16_t)(v[off] | ((uint16_t)v[off + 1] << 8))
            : 0xFFFF;
    };
    const uint16_t im2 = (uint16_t)cpu.i << 8;
    const auto im2_00 = emu.read_debug_memory((uint16_t)(im2 + 0x00), 2);
    const auto im2_8c = emu.read_debug_memory((uint16_t)(im2 + 0x8c), 2);
    const auto im2_a0 = emu.read_debug_memory((uint16_t)(im2 + 0xa0), 2);
    const uint16_t cur = rd16_at(threads, 0);
    const uint16_t run = rd16_at(threads, 4);
    const uint16_t wait = rd16_at(threads, 6);
    const uint16_t term = rd16_at(threads, 8);
    const auto cur_bytes = emu.read_debug_memory(cur, 27);
    const auto run_bytes = emu.read_debug_memory(run, 27);
    const auto wait_bytes = emu.read_debug_memory(wait, 27);
    std::printf(
        "        debug: pc=%04X sp=%04X iff1=%u halted=%u "
        "ir_ref=%02X armed=%02X cur=%04X run=%04X wait=%04X term=%04X "
        "cur[n=%04X st=%02X pr=%04X] run[n=%04X st=%02X pr=%04X] "
        "wait[n=%04X st=%02X pr=%04X ev=%04X] "
        "i=%02X im2[00]=%04X im2[8c]=%04X im2[a0]=%04X "
        "sio0a wr1=%02X rr0=%02X rx_ready=%u tx_ready=%u int_state=%02X data=%02X "
        "pins=%016llX step=%u ack_count=%u ack=[%02X@%04X %02X@%04X %02X@%04X %02X@%04X]\n",
        emu.get_current_pc(),
        cpu.sp,
        cpu.iff1 ? 1U : 0U,
        cpu.halted ? 1U : 0U,
        ir_ref,
        ir_armed,
        cur,
        run,
        wait,
        term,
        rd16_at(cur_bytes, 0),
        cur_bytes.size() >= 20 ? cur_bytes[19] : 0xFF,
        rd16_at(cur_bytes, 22),
        rd16_at(run_bytes, 0),
        run_bytes.size() >= 20 ? run_bytes[19] : 0xFF,
        rd16_at(run_bytes, 22),
        rd16_at(wait_bytes, 0),
        wait_bytes.size() >= 20 ? wait_bytes[19] : 0xFF,
        rd16_at(wait_bytes, 22),
        rd16_at(wait_bytes, 25),
        cpu.i,
        rd16_at(im2_00, 0),
        rd16_at(im2_8c, 0),
        rd16_at(im2_a0, 0),
        ch.wr[1],
        ch.rr[0],
        ch.rx_ready ? 1U : 0U,
        ch.tx_ready ? 1U : 0U,
        ch.int_state,
        ch.rx_data,
        (unsigned long long)emu.get_pins(),
        (unsigned)emu.cpu.step,
        emu.dbg_im2_ack_count,
        emu.dbg_im2_ack_vectors[(emu.dbg_im2_ack_count + 4) & 0x7u],
        emu.dbg_im2_ack_pcs[(emu.dbg_im2_ack_count + 4) & 0x7u],
        emu.dbg_im2_ack_vectors[(emu.dbg_im2_ack_count + 5) & 0x7u],
        emu.dbg_im2_ack_pcs[(emu.dbg_im2_ack_count + 5) & 0x7u],
        emu.dbg_im2_ack_vectors[(emu.dbg_im2_ack_count + 6) & 0x7u],
        emu.dbg_im2_ack_pcs[(emu.dbg_im2_ack_count + 6) & 0x7u],
        emu.dbg_im2_ack_vectors[(emu.dbg_im2_ack_count + 7) & 0x7u],
        emu.dbg_im2_ack_pcs[(emu.dbg_im2_ack_count + 7) & 0x7u]);
}

static step_result run_step(regression_partner_crt &emu,
                            const step &s,
                            uint64_t key_ticks,
                            uint64_t step_budget)
{
    step_result r;
    const size_t before = emu.dump_raw_serial_text().size();
    const uint64_t start_tick = emu.get_tick_count();

    // Type the command, then the carriage return, echoing each key like a
    // real operator; run a few thousand ticks between keys so the shell's
    // SIO read loop consumes them.
    for (char ch : s.cmd) {
        emu.key_input((uint8_t)ch);
        for (uint64_t n = 0; n < key_ticks; ++n)
            emu.tick();
    }
    emu.key_input((uint8_t)'\r');

    // Wait for the prompt to return (i.e. the command finished) or freeze.
    uint64_t waited = 0;
    r.timeout = true;
    while (waited < step_budget) {
        for (uint64_t n = 0; n < POLL_INTERVAL; ++n)
            emu.tick();
        waited += POLL_INTERVAL;
        if (prompt_returned_after(emu, before)) {
            r.timeout = false;
            break;
        }
    }

    r.ticks = emu.get_tick_count() - start_tick;
    const std::string raw = emu.dump_raw_serial_text();
    r.delta = raw.size() > before ? raw.substr(before) : std::string{};
    if (r.timeout && debug_timeout_enabled())
        dump_timeout_state(emu);

    for (const auto &e : s.expect) {
        if (!contains(r.delta, e))
            r.missing.push_back(e);
    }
    for (const auto &f : s.forbid) {
        if (contains(r.delta, f))
            r.present.push_back(f);
    }
    r.pass = !r.timeout && r.missing.empty() && r.present.empty();
    return r;
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

    const bool record = std::getenv("IDP_RECORD") != nullptr;
    const uint64_t key_ticks =
        env_u64_allow_zero_or("IDP_KEY_TICKS", DEFAULT_KEY_TICKS);
    const uint64_t step_budget = env_u64_or("IDP_STEP_BUDGET", DEFAULT_STEP_BUDGET);
    const uint64_t stress_rounds =
        env_u64_allow_zero_or("IDP_STRESS_ROUNDS", 0);

    const std::string rom_path = PARTOS_ROOT "/bin/partos.rom";
    const std::string hdd_path = PARTOS_ROOT "/bin/disks/hdd-dos.img";

    // The scripted regression walk. Every step is a real command; success is
    // silent for the mutating commands, so those simply forbid the error
    // marker '?' (and 'usage' from an argc mismatch).
    std::vector<step> script = make_smoke_script();
    append_stress_rounds(script, (size_t)stress_rounds);

    regression_partner_crt emu(terminal_profile::vt52,
                               PARTOS_ROOT "/partos_shadow_nvram.bin");
    emu.load_rom(rom_path);
    emu.load_hdd(hdd_path);
    emu.reset();

    if (!boot_to_shell(emu))
        return 1;

    std::printf("PartOS shell regression pack: %zu steps "
                "(key_ticks=%llu step_budget=%llu stress_rounds=%llu%s)\n\n",
                script.size(),
                (unsigned long long)key_ticks,
                (unsigned long long)step_budget,
                (unsigned long long)stress_rounds,
                record ? " RECORD" : "");

    size_t failures = 0;
    for (size_t i = 0; i < script.size(); ++i) {
        const step &s = script[i];
        const step_result r = run_step(emu, s, key_ticks, step_budget);
        if (!r.pass)
            ++failures;

        const char *tag = r.pass ? "PASS" : (r.timeout ? "FREEZE" : "FAIL");
        std::printf("[%02zu] %-6s $ %-22s (%llu ticks)  %s\n",
                    i + 1, tag, s.cmd.c_str(),
                    (unsigned long long)r.ticks, s.note.c_str());

        if (!r.pass || record) {
            for (const auto &m : r.missing)
                std::printf("        missing expected : \"%s\"\n", m.c_str());
            for (const auto &p : r.present)
                std::printf("        found forbidden  : \"%s\"\n", p.c_str());
            if (r.timeout)
                std::printf("        NO PROMPT RETURN within %llu ticks "
                            "(shell frozen / command hung)\n",
                            (unsigned long long)step_budget);
            std::printf("        output: %s\n", visualize(r.delta).c_str());
        }
    }

    std::printf("\n%s: %zu/%zu steps passed\n",
                failures == 0 ? "PASS" : "FAIL",
                script.size() - failures, script.size());

    if (record)
        return 0;
    return (int)failures;
}
