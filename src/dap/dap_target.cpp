// dap_target.cpp - partner machine debug target for udap's libdap.
#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>

#include <sdcc/cdb_parser.h>
#include <sdcc/map_parser.h>

#include "z80dasm.h"

#include "dap_debugger.hpp"
#include "dap_target.hpp"

namespace fs = std::filesystem;

namespace {

bool is_call_op(uint8_t op)
{
    return op == 0xCD || (op & 0xC7) == 0xC4 || (op & 0xC7) == 0xC7;
}

bool is_ret_op(uint8_t op, uint8_t op2)
{
    return op == 0xC9 || (op & 0xC7) == 0xC0
        || (op == 0xED && (op2 == 0x4D || op2 == 0x45));
}

// Run the machine until it stands at the start of a different instruction.
// Covers all stop states: at a boundary, mid-instruction, or freshly
// redirected via debug_set_pc. Repeating block instructions (LDIR, ...)
// re-fetch at the same PC until done, so they complete as one atomic step.
void tick_one_instruction(partner &emu)
{
    const uint16_t start_pc = emu.get_current_pc();
    static constexpr uint32_t kGuard = 1000000;
    for (uint32_t guard = 0; guard < kGuard; ++guard) {
        emu.tick();
        if (emu.is_opdone()) {
            if (emu.get_current_pc() != start_pc)
                return;
            if (emu.capture_debug_cpu_state().halted)
                return; // HALT spins at the same PC; don't burn the guard
        }
    }
}

// ---------------------------------------------------------------------------
// Program loading (mirrors udap's z80_target launch loader, but writes into
// the live partner memory instead of a private 64 KB buffer).
// ---------------------------------------------------------------------------

struct ihx_load_result {
    uint16_t entry = 0;
    bool explicit_start = false;
};

ihx_load_result load_ihx(std::istream &in, partner &emu)
{
    uint32_t upper_base = 0;
    uint32_t lowest_data_addr = 0xFFFFFFFF;
    std::optional<uint32_t> explicit_entry;

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] != ':' || line.size() < 11)
            continue;

        auto hex2 = [&](size_t pos) -> uint8_t {
            return static_cast<uint8_t>(std::stoul(line.substr(pos, 2), nullptr, 16));
        };
        auto hex4 = [&](size_t pos) -> uint16_t {
            return static_cast<uint16_t>((hex2(pos) << 8) | hex2(pos + 2));
        };

        uint8_t byte_count = 0;
        uint16_t address = 0;
        uint8_t rec_type = 0;
        try {
            byte_count = hex2(1); address = hex4(3); rec_type = hex2(7);
        } catch (...) { continue; }

        if (line.size() < (11 + static_cast<size_t>(byte_count) * 2))
            continue;
        if (rec_type == 0x01)
            break;

        if (rec_type == 0x00) {
            uint32_t base = upper_base + address;
            if (byte_count > 0 && base < lowest_data_addr)
                lowest_data_addr = base;
            std::vector<uint8_t> bytes;
            bytes.reserve(byte_count);
            for (uint8_t i = 0; i < byte_count; ++i) {
                try { bytes.push_back(hex2(9 + static_cast<size_t>(i) * 2)); }
                catch (...) { break; }
            }
            emu.write_debug_memory(base, bytes);
        } else if (rec_type == 0x02 && byte_count >= 2) {
            try { upper_base = static_cast<uint32_t>(hex4(9)) << 4; } catch (...) {}
        } else if (rec_type == 0x04 && byte_count >= 2) {
            try { upper_base = static_cast<uint32_t>(hex4(9)) << 16; } catch (...) {}
        } else if (rec_type == 0x03 && byte_count >= 4) {
            try { explicit_entry = (static_cast<uint32_t>(hex4(9)) << 4) + hex4(13); }
            catch (...) {}
        } else if (rec_type == 0x05 && byte_count >= 4) {
            try {
                explicit_entry = (static_cast<uint32_t>(hex2(9))  << 24) |
                                 (static_cast<uint32_t>(hex2(11)) << 16) |
                                 (static_cast<uint32_t>(hex2(13)) <<  8) |
                                  static_cast<uint32_t>(hex2(15));
            } catch (...) {}
        }
    }

    ihx_load_result result;
    if (explicit_entry) {
        result.entry = static_cast<uint16_t>(*explicit_entry & 0xFFFF);
        result.explicit_start = true;
    } else if (lowest_data_addr != 0xFFFFFFFF) {
        result.entry = static_cast<uint16_t>(lowest_data_addr & 0xFFFF);
    }
    return result;
}

} // namespace

partner_target::partner_target(partner &emu, dap_debugger &host)
    : emu_(emu), host_(host), dbg_(emu)
{
}

// ---------------------------------------------------------------------------
// Launch
// ---------------------------------------------------------------------------

bool partner_target::launch(const dap::launch_args &args)
{
    // The machine must be standing still before the program is written into
    // its memory; otherwise the CPU would execute half-loaded code.
    host_.request_pause(/*silent=*/true);
    if (!host_.wait_until_paused(2000))
        std::cerr << "[dap] warning: emulator did not pause before launch\n";

    std::lock_guard<std::recursive_mutex> lock(host_.mutex());

    dbg_.clear_full_listing();
    dbg_.set_cdb_modules({});
    dbg_.set_source_roots({});
    dbg_.set_map_symbols({});
    dbg_.set_map_segments({});

    uint16_t entry = args.start_address.value_or(0x0000);
    std::string entry_reason = args.start_address ? "from launch startAddress" : "default 0x0000";

    if (!args.program.empty()) {
        std::string bin_path = args.program;
        std::string ext;
        try { ext = fs::path(bin_path).extension().string(); } catch (...) {}

        std::ifstream bin_file(bin_path,
            (ext == ".ihx" || ext == ".hex") ? std::ios::in : std::ios::binary);
        if (bin_file) {
            if (ext == ".ihx" || ext == ".hex") {
                auto res = load_ihx(bin_file, emu_);
                if (!args.start_address) {
                    entry = res.entry;
                    entry_reason = res.explicit_start
                        ? "from IHX start address record"
                        : "from IHX lowest data address";
                }
            } else {
                // Raw binary: load at startAddress (or 0x0000).
                std::vector<uint8_t> data(
                    (std::istreambuf_iterator<char>(bin_file)),
                    std::istreambuf_iterator<char>());
                if (data.size() > partner::ram_size)
                    data.resize(partner::ram_size);
                emu_.write_debug_memory(entry, data);
            }
            std::cerr << "[dap] Loaded: " << bin_path << "\n";
        } else {
            std::cerr << "[dap] ERROR: Cannot open: " << bin_path << "\n";
        }

        // Set source roots BEFORE loading CDB/MAP so the source index
        // resolves relative paths correctly.
        std::string root = args.source_root.empty()
            ? fs::path(bin_path).parent_path().string()
            : args.source_root;
        dbg_.set_source_root(root);
        dbg_.set_source_roots(args.source_roots);

        fs::path cdb_path = args.cdb_file.empty()
            ? fs::path(bin_path).replace_extension(".cdb")
            : fs::path(args.cdb_file);
        if (fs::exists(cdb_path)) {
            sdcc::cdb_parser parser;
            auto modules = parser.parse(cdb_path.string());
            if (modules) {
                std::cerr << "[dap] Loaded CDB: " << cdb_path.string()
                          << " (" << modules->size() << " modules)\n";
                dbg_.set_cdb_modules(std::move(*modules));
            }
        }

        fs::path map_path = args.map_file.empty()
            ? fs::path(bin_path).replace_extension(".map")
            : fs::path(args.map_file);
        if (fs::exists(map_path)) {
            sdcc::map_parser parser;
            auto map = parser.parse(map_path.string());
            if (map) {
                dbg_.set_map_symbols(map->symbols);
                dbg_.set_map_segments(map->segments);
                std::cerr << "[dap] Loaded MAP: " << map_path.string() << "\n";
            }
        }

        dbg_.rebuild_source_breakpoint_addresses();

        std::string base;
        try { base = fs::path(bin_path).stem().string(); } catch (...) { base = "listing"; }
        dbg_.set_virtual_lst_path("/__virtual__/" + base + ".asm");
        dbg_.build_full_listing();

        // Jump to the program entry. SP is left alone: the machine keeps its
        // live environment (ROM monitor / OS) and the program's own startup
        // code sets the stack it wants. Interrupts are disabled so pending
        // OS interrupts cannot hijack the injected program; it can EI itself.
        emu_.debug_set_pc(entry);
        auto cpu = emu_.capture_debug_cpu_state();
        cpu.pc = entry;
        cpu.iff1 = false;
        cpu.iff2 = false;
        cpu.halted = false;
        emu_.apply_debug_cpu_state(cpu);
        std::cerr << "[dap] Entry: 0x" << std::hex << entry << std::dec
                  << " (" << entry_reason << ")\n";
    } else {
        // No program: attach to whatever the machine is running right now.
        dbg_.set_virtual_lst_path("/__virtual__/listing.asm");
        dbg_.build_full_listing();
        std::cerr << "[dap] Attached at PC=0x" << std::hex
                  << emu_.get_current_pc() << std::dec << "\n";
    }

    return true;
}

void partner_target::disconnect()
{
    // The emulator keeps running under main-loop control; just drop any
    // outstanding pause request so the session does not leave the machine
    // wedged.
    std::lock_guard<std::recursive_mutex> lock(host_.mutex());
    dbg_.set_asm_breakpoints({});
    dbg_.instruction_breakpoints().clear();
    dbg_.function_breakpoints().clear();
    dbg_.rebuild_all_breakpoints();
}

// ---------------------------------------------------------------------------
// Execution
// ---------------------------------------------------------------------------

void partner_target::resume()
{
    host_.request_continue();
}

void partner_target::pause()
{
    host_.request_pause(/*silent=*/false);
}

void partner_target::step_machine_instruction()
{
    tick_one_instruction(emu_);
}

void partner_target::step()
{
    std::lock_guard<std::recursive_mutex> lock(host_.mutex());
    dbg_.push_history();

    uint16_t start_pc = emu_.get_current_pc();
    auto start_loc = dbg_.lookup_source_any(start_pc);

    auto step_over_loop = [&](auto should_stop) {
        int call_depth = 0;
        int16_t start_sp = static_cast<int16_t>(emu_.capture_debug_cpu_state().sp);
        for (int i = 0; i < 500000; ++i) {
            uint16_t pc = emu_.get_current_pc();
            uint8_t op = emu_.peek_mem(pc);
            uint8_t op2 = emu_.peek_mem(static_cast<uint16_t>(pc + 1));
            int16_t sp_before = static_cast<int16_t>(emu_.capture_debug_cpu_state().sp);
            step_machine_instruction();
            int16_t sp_after = static_cast<int16_t>(emu_.capture_debug_cpu_state().sp);
            int16_t sp_delta = sp_after - sp_before;
            if (sp_delta == -2 && is_call_op(op))
                call_depth++;
            else if (sp_delta == 2 && is_ret_op(op, op2) && call_depth > 0)
                call_depth--;
            if (sp_after >= start_sp)
                call_depth = 0;
            if (call_depth > 0)
                continue;
            if (should_stop(emu_.get_current_pc()))
                break;
        }
    };

    if (start_loc && start_loc->is_asm) {
        step_over_loop([&](uint16_t pc) {
            auto loc = dbg_.lookup_source_any(pc);
            return loc && (loc->file != start_loc->file || loc->line != start_loc->line);
        });
    } else if (start_loc) {
        step_over_loop([&](uint16_t pc) {
            auto loc = dbg_.lookup_source(pc);
            return loc && (loc->file != start_loc->file || loc->line != start_loc->line);
        });
    } else {
        // No CDB mapping at current PC (virtual listing / unmapped epilogue).
        // Walk forward until we find a mapped location or hit a limit.
        int16_t entry_sp = static_cast<int16_t>(emu_.capture_debug_cpu_state().sp);
        for (int i = 0; i < 32; ++i) {
            step_machine_instruction();
            if (dbg_.lookup_source_any(emu_.get_current_pc()))
                break; // reached a mapped location
            if (static_cast<int16_t>(emu_.capture_debug_cpu_state().sp) > entry_sp)
                break; // RET returned us to caller
            if (!dbg_.has_cdb() && !dbg_.has_map())
                break; // pure machine-level session: one instruction per step
        }
    }
}

void partner_target::step_in()
{
    std::lock_guard<std::recursive_mutex> lock(host_.mutex());
    dbg_.push_history();

    uint16_t start_pc = emu_.get_current_pc();
    auto start_loc = dbg_.lookup_source_any(start_pc);

    if (start_loc && start_loc->is_asm) {
        for (int i = 0; i < 10000; ++i) {
            step_machine_instruction();
            auto loc = dbg_.lookup_source_any(emu_.get_current_pc());
            if (loc && (loc->file != start_loc->file || loc->line != start_loc->line))
                break;
        }
    } else if (start_loc && dbg_.has_cdb()) {
        for (int i = 0; i < 10000; ++i) {
            uint16_t pc = emu_.get_current_pc();
            uint8_t op = emu_.peek_mem(pc);
            int16_t sp_before = static_cast<int16_t>(emu_.capture_debug_cpu_state().sp);
            step_machine_instruction();
            int16_t sp_after = static_cast<int16_t>(emu_.capture_debug_cpu_state().sp);
            uint16_t new_pc = emu_.get_current_pc();
            auto loc = dbg_.lookup_source(new_pc);
            if (loc && (loc->file != start_loc->file || loc->line != start_loc->line))
                break;
            if (!loc) {
                bool entered_call = (sp_after - sp_before == -2) && is_call_op(op);
                if (entered_call && dbg_.lookup_source_any(new_pc))
                    break;
            }
        }
    } else {
        step_machine_instruction();
    }
}

void partner_target::step_out()
{
    std::lock_guard<std::recursive_mutex> lock(host_.mutex());
    dbg_.push_history();
    int16_t entry_sp = static_cast<int16_t>(emu_.capture_debug_cpu_state().sp);
    for (int i = 0; i < 500000; ++i) {
        step_machine_instruction();
        if (static_cast<int16_t>(emu_.capture_debug_cpu_state().sp) > entry_sp)
            break;
    }
}

void partner_target::step_back()
{
    std::lock_guard<std::recursive_mutex> lock(host_.mutex());
    dbg_.pop_history();
}

// ---------------------------------------------------------------------------
// State queries
// ---------------------------------------------------------------------------

std::vector<uint8_t> partner_target::read_memory(uint16_t addr, int count) const
{
    std::lock_guard<std::recursive_mutex> lock(host_.mutex());
    uint32_t avail = static_cast<uint32_t>(partner::ram_size) - addr;
    uint32_t n = std::min(static_cast<uint32_t>(std::max(0, count)), avail);
    return emu_.read_debug_memory(addr, n);
}

std::vector<dap::frame_info> partner_target::get_stack() const
{
    std::lock_guard<std::recursive_mutex> lock(host_.mutex());
    uint16_t pc = emu_.get_current_pc();
    dap::frame_info f;
    f.address = dap_dbg::format_hex(pc, 4);

    auto src = dbg_.has_cdb() ? dbg_.lookup_source_any(pc) : std::nullopt;
    if (src) {
        f.source_path = src->file;
        f.name = fs::path(src->file).filename().string()
                 + ":" + std::to_string(src->line);
        f.line = src->line;
    } else {
        f.source_path = dbg_.virtual_lst_path();
        f.name = dbg_.lookup_symbol(pc).value_or(dap_dbg::format_hex(pc, 4));
        f.line = dbg_.full_listing_line_for_addr(pc);
    }
    return {f};
}

std::vector<dap::scope_info> partner_target::get_scopes() const
{
    std::lock_guard<std::recursive_mutex> lock(host_.mutex());
    std::vector<dap::scope_info> scopes;
    scopes.push_back({"Registers", "registers"});

    if (dbg_.has_cdb()) {
        auto *fn = dbg_.lookup_function_at(emu_.get_current_pc());
        if (fn && !fn->local_symbols.empty())
            scopes.push_back({"Locals", "locals"});
        scopes.push_back({"Globals", "locals"});
    }

    if (dbg_.has_map()) {
        scopes.push_back({"MAP Segments", "locals"});
        scopes.push_back({"MAP Symbols", "locals"});
    }

    return scopes;
}

std::vector<dap::variable_info> partner_target::get_variables(const std::string &scope) const
{
    std::lock_guard<std::recursive_mutex> lock(host_.mutex());
    std::vector<dap::variable_info> vars;

    auto make_reg = [](const char *name, uint16_t val, int width, bool with_mem) {
        dap::variable_info v;
        v.name = name;
        v.value = dap_dbg::format_hex(val, width);
        if (with_mem)
            v.memory_reference = dap_dbg::format_hex(val, 4);
        return v;
    };

    if (scope == "Registers") {
        const auto cpu = emu_.capture_debug_cpu_state();
        const uint16_t pc = emu_.get_current_pc();
        vars.push_back(make_reg("AF", cpu.af, 4, false));
        vars.push_back(make_reg("BC", cpu.bc, 4, true));
        vars.push_back(make_reg("DE", cpu.de, 4, true));
        vars.push_back(make_reg("HL", cpu.hl, 4, true));
        vars.push_back(make_reg("IX", cpu.ix, 4, true));
        vars.push_back(make_reg("IY", cpu.iy, 4, true));
        vars.push_back(make_reg("SP", cpu.sp, 4, true));
        vars.push_back(make_reg("PC", pc, 4, true));
        vars.push_back(make_reg("R", cpu.r, 2, false));
        vars.push_back(make_reg("I", cpu.i, 2, false));
        vars.push_back(make_reg("F", static_cast<uint16_t>(cpu.af & 0xFF), 2, false));

    } else if (scope == "Locals") {
        auto *fn = dbg_.lookup_function_at(emu_.get_current_pc());
        if (fn)
            for (const auto &sym : fn->local_symbols)
                vars.push_back({sym.name, dbg_.c_variable_value(sym), sym.type_info, std::nullopt});

    } else if (scope == "Globals") {
        for (const auto &mod : dbg_.cdb_modules())
            for (const auto &sym : mod.global_symbols) {
                if (sym.type_info.find("DF") != std::string::npos)
                    continue;
                vars.push_back({sym.name, dbg_.c_variable_value(sym), sym.type_info, std::nullopt});
            }

    } else if (scope == "MAP Segments") {
        for (const auto &seg : dbg_.map_segments()) {
            uint16_t addr = static_cast<uint16_t>(seg.address & 0xFFFF);
            vars.push_back({seg.name,
                "addr=" + dap_dbg::format_hex(addr, 4)
                + ", size=" + dap_dbg::format_hex(static_cast<uint16_t>(seg.size & 0xFFFF), 4)
                + ", " + seg.attributes,
                "", dap_dbg::format_hex(addr, 4)});
        }

    } else if (scope == "MAP Symbols") {
        for (const auto &sym : dbg_.map_symbols()) {
            uint16_t addr = static_cast<uint16_t>(sym.address & 0xFFFF);
            vars.push_back({sym.name, dap_dbg::format_hex(addr, 4), "", dap_dbg::format_hex(addr, 4)});
        }
    }

    return vars;
}

// ---------------------------------------------------------------------------
// Breakpoints
// ---------------------------------------------------------------------------

std::vector<dap::breakpoint_info> partner_target::set_source_breakpoints(
    const std::string &path, int source_reference, const std::vector<int> &lines)
{
    (void)source_reference;
    std::lock_guard<std::recursive_mutex> lock(host_.mutex());

    bool is_virtual = (!path.empty() && path == dbg_.virtual_lst_path());

    if (is_virtual) {
        std::vector<dap::breakpoint_info> result;
        std::vector<uint16_t> addrs;
        const auto &line_addrs = dbg_.full_listing_addrs();
        for (int line : lines) {
            int idx = line - 1;
            if (idx >= 0 && static_cast<size_t>(idx) < line_addrs.size()) {
                uint16_t addr = line_addrs[static_cast<size_t>(idx)];
                addrs.push_back(addr);
                result.push_back({true, line, "", dap_dbg::format_hex(addr, 4)});
            } else {
                result.push_back({false, line, "Line out of range", std::nullopt});
            }
        }
        dbg_.set_asm_breakpoints(std::move(addrs));
        return result;
    }

    dbg_.set_source_breakpoints_for_file(path, lines);
    auto raw = dbg_.resolve_source_breakpoints_for_file(path);
    dbg_.rebuild_source_breakpoint_addresses();

    std::vector<dap::breakpoint_info> result;
    for (const auto &bp : raw)
        result.push_back({bp.verified, bp.line, bp.message, std::nullopt});
    return result;
}

std::vector<dap::breakpoint_info> partner_target::set_function_breakpoints(
    const std::vector<std::string> &names)
{
    std::lock_guard<std::recursive_mutex> lock(host_.mutex());
    dbg_.function_breakpoints().clear();
    std::vector<dap::breakpoint_info> result;
    for (const auto &name : names) {
        auto addr = dbg_.lookup_function_address(name);
        if (addr) {
            dbg_.function_breakpoints().push_back(*addr);
            result.push_back({true, 0, "", dap_dbg::format_hex(*addr, 4)});
        } else {
            result.push_back({false, 0, "Function not found: " + name, std::nullopt});
        }
    }
    dbg_.rebuild_all_breakpoints();
    return result;
}

std::vector<dap::breakpoint_info> partner_target::set_instruction_breakpoints(
    const std::vector<std::string> &instruction_references)
{
    std::lock_guard<std::recursive_mutex> lock(host_.mutex());
    dbg_.instruction_breakpoints().clear();
    for (const auto &ref : instruction_references) {
        try {
            dbg_.instruction_breakpoints().push_back(
                static_cast<uint16_t>(std::stoul(ref, nullptr, 0)));
        } catch (...) {}
    }
    dbg_.rebuild_all_breakpoints();

    std::vector<dap::breakpoint_info> result;
    for (uint16_t addr : dbg_.instruction_breakpoints())
        result.push_back({true, 0, "", dap_dbg::format_hex(addr, 4)});
    return result;
}

// ---------------------------------------------------------------------------
// Source content
// ---------------------------------------------------------------------------

std::optional<dap::source_info> partner_target::get_source(const std::string &path) const
{
    std::lock_guard<std::recursive_mutex> lock(host_.mutex());

    if (path == dbg_.virtual_lst_path()) {
        if (!dbg_.has_full_listing())
            dbg_.build_full_listing();
        return dap::source_info{dbg_.full_listing_content(), "text/x-asm"};
    }

    auto resolved = dbg_.resolve_source_path(path);
    std::string real_path = resolved ? *resolved : path;

    std::ifstream ifs(real_path);
    if (!ifs)
        return std::nullopt;
    std::ostringstream ss;
    ss << ifs.rdbuf();

    // Always "text/x-c": the VSCode C++ extension registers gutter-breakpoint
    // support for that MIME type but not for "text/x-asm".
    return dap::source_info{ss.str(), "text/x-c"};
}

// ---------------------------------------------------------------------------
// Disassembly
// ---------------------------------------------------------------------------

namespace {

struct live_dasm_ctx {
    const partner *emu = nullptr;
    uint32_t addr = 0;
    std::string text;
};

uint8_t live_dasm_in(void *user_data)
{
    auto *ctx = static_cast<live_dasm_ctx *>(user_data);
    uint8_t byte = ctx->emu->peek_mem(static_cast<uint16_t>(ctx->addr & 0xFFFF));
    ctx->addr++;
    return byte;
}

void live_dasm_out(char c, void *user_data)
{
    static_cast<live_dasm_ctx *>(user_data)->text.push_back(c);
}

// Disassemble one instruction at addr; returns its mnemonic and length.
std::pair<std::string, uint32_t> dasm_one(const partner &emu, uint32_t addr)
{
    live_dasm_ctx ctx;
    ctx.emu = &emu;
    ctx.addr = addr;
    z80dasm_op(static_cast<uint16_t>(addr & 0xFFFF), live_dasm_in, live_dasm_out, &ctx);
    uint32_t len = ctx.addr - addr;
    if (len == 0)
        len = 1;
    return {std::move(ctx.text), len};
}

} // namespace

std::vector<dap::disasm_info> partner_target::disassemble(
    int memory_reference, int offset,
    int instruction_offset, int instruction_count) const
{
    std::lock_guard<std::recursive_mutex> lock(host_.mutex());

    uint16_t base_addr = static_cast<uint16_t>((memory_reference + offset) & 0xFFFF);
    uint32_t addr = base_addr;

    if (instruction_offset < 0) {
        int backstep = -instruction_offset;
        int rewind = std::min(static_cast<int>(addr), backstep * 4);
        uint32_t scan = addr - static_cast<uint32_t>(rewind);
        std::vector<uint32_t> addrs;
        uint32_t pos = scan;
        while (pos <= addr + 3 && pos < partner::ram_size) {
            addrs.push_back(pos);
            pos += dasm_one(emu_, pos).second;
        }
        int target_idx = static_cast<int>(addrs.size()) - 1;
        for (int i = 0; i < static_cast<int>(addrs.size()); ++i)
            if (addrs[i] >= addr) { target_idx = i; break; }
        addr = addrs[static_cast<size_t>(std::max(0, target_idx - backstep))];
    } else if (instruction_offset > 0) {
        for (int i = 0; i < instruction_offset && addr < partner::ram_size; ++i)
            addr += dasm_one(emu_, addr).second;
    }

    std::vector<dap::disasm_info> result;
    for (int i = 0; i < instruction_count && addr < partner::ram_size; ++i) {
        auto [mnemonic, ilen] = dasm_one(emu_, addr);

        std::ostringstream bytes_oss;
        for (uint32_t j = 0; j < ilen && (addr + j) < partner::ram_size; ++j) {
            if (j > 0)
                bytes_oss << " ";
            bytes_oss << std::uppercase << std::setw(2) << std::setfill('0')
                      << std::hex
                      << static_cast<int>(emu_.peek_mem(static_cast<uint16_t>(addr + j)));
        }

        dap::disasm_info info;
        info.address = dap_dbg::format_hex(static_cast<uint16_t>(addr), 4);
        info.instruction_bytes = bytes_oss.str();
        info.instruction = mnemonic;

        auto sym = dbg_.lookup_symbol_exact(static_cast<uint16_t>(addr));
        if (sym)
            info.symbol = *sym;

        auto loc = dbg_.lookup_source(static_cast<uint16_t>(addr));
        if (loc) {
            info.source_path = loc->file;
            info.source_line = loc->line;
        }

        result.push_back(info);
        addr += ilen;
    }
    return result;
}

// ---------------------------------------------------------------------------
// Evaluate
// ---------------------------------------------------------------------------

dap::eval_info partner_target::evaluate(const std::string &expr) const
{
    std::lock_guard<std::recursive_mutex> lock(host_.mutex());

    std::string upper = expr;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);

    const auto cpu = emu_.capture_debug_cpu_state();
    const uint16_t pc = emu_.get_current_pc();

    struct reg_entry { const char *name; uint16_t value; int width; };
    const reg_entry regs[] = {
        {"AF", cpu.af, 4}, {"BC", cpu.bc, 4}, {"DE", cpu.de, 4},
        {"HL", cpu.hl, 4}, {"IX", cpu.ix, 4}, {"IY", cpu.iy, 4},
        {"SP", cpu.sp, 4}, {"PC", pc, 4},
        {"A", static_cast<uint16_t>((cpu.af >> 8) & 0xFF), 2},
        {"F", static_cast<uint16_t>(cpu.af & 0xFF), 2},
        {"B", static_cast<uint16_t>((cpu.bc >> 8) & 0xFF), 2},
        {"C", static_cast<uint16_t>(cpu.bc & 0xFF), 2},
        {"D", static_cast<uint16_t>((cpu.de >> 8) & 0xFF), 2},
        {"E", static_cast<uint16_t>(cpu.de & 0xFF), 2},
        {"H", static_cast<uint16_t>((cpu.hl >> 8) & 0xFF), 2},
        {"L", static_cast<uint16_t>(cpu.hl & 0xFF), 2},
        {"R", cpu.r, 2}, {"I", cpu.i, 2},
    };

    for (const auto &reg : regs) {
        if (upper != reg.name)
            continue;
        dap::eval_info r;
        r.result = dap_dbg::format_hex(reg.value, reg.width);
        if (reg.width == 4)
            r.memory_reference = dap_dbg::format_hex(reg.value, 4);
        return r;
    }

    for (const auto &sym : dbg_.map_symbols()) {
        if (sym.name == expr || sym.name == "_" + expr) {
            uint16_t addr = static_cast<uint16_t>(sym.address & 0xFFFF);
            return {true, dap_dbg::format_hex(addr, 4), "", dap_dbg::format_hex(addr, 4)};
        }
    }

    try {
        uint16_t val = 0;
        if (expr.size() > 2 && expr[0] == '0' && (expr[1] == 'x' || expr[1] == 'X'))
            val = static_cast<uint16_t>(std::stoul(expr, nullptr, 0));
        else if (expr.size() > 1 && expr[0] == '$')
            val = static_cast<uint16_t>(std::stoul(expr.substr(1), nullptr, 16));
        else
            return {false, "", "Cannot evaluate: " + expr, std::nullopt};
        uint8_t byte = emu_.peek_mem(val);
        return {true, dap_dbg::format_hex(byte, 2), "", dap_dbg::format_hex(val, 4)};
    } catch (...) {}

    return {false, "", "Cannot evaluate: " + expr, std::nullopt};
}

// ---------------------------------------------------------------------------
// Breakpoint locations
// ---------------------------------------------------------------------------

std::vector<dap::bp_location_info> partner_target::get_breakpoint_locations(
    const std::string &path, int line, int end_line) const
{
    std::lock_guard<std::recursive_mutex> lock(host_.mutex());

    std::string query_name = fs::path(path).filename().string();
    std::set<int> valid_lines;

    for (const auto &mod : dbg_.cdb_modules()) {
        // C source lines.
        for (const auto &ln : mod.lines)
            if (ln.line >= line && ln.line <= end_line &&
                fs::path(ln.file).filename().string() == query_name)
                valid_lines.insert(ln.line);

        // Assembly source lines (without these, the .s gutter is disabled).
        if (!mod.asm_lines.empty()) {
            auto resolved = dbg_.resolve_source_path(mod.file);
            std::string asm_name = fs::path(
                resolved ? *resolved : mod.file).filename().string();
            if (asm_name == query_name)
                for (const auto &[line_num, addr] : mod.asm_lines)
                    if (line_num >= line && line_num <= end_line)
                        valid_lines.insert(line_num);
        }
    }

    for (const auto &sym : dbg_.map_symbols()) {
        auto loc = dbg_.map_symbol_to_source(sym);
        if (!loc || loc->line < line || loc->line > end_line)
            continue;
        if (fs::path(loc->file).filename().string() == query_name)
            valid_lines.insert(loc->line);
    }

    std::vector<dap::bp_location_info> result;
    for (int l : valid_lines)
        result.push_back({l});
    return result;
}

// ---------------------------------------------------------------------------
// Loaded sources
// ---------------------------------------------------------------------------

std::vector<dap::loaded_source_info> partner_target::get_loaded_sources() const
{
    std::lock_guard<std::recursive_mutex> lock(host_.mutex());

    std::set<std::string> seen;
    std::vector<dap::loaded_source_info> result;

    auto add = [&](const std::string &file) {
        auto resolved = dbg_.resolve_source_path(file);
        std::string path = resolved ? *resolved : file;
        if (!seen.insert(path).second)
            return;
        result.push_back({fs::path(path).filename().string(), path});
    };

    for (const auto &mod : dbg_.cdb_modules()) {
        if (!mod.file.empty())
            add(mod.file);
        for (const auto &ln : mod.lines)
            if (!ln.file.empty())
                add(ln.file);
    }
    return result;
}
