// dap_dbg.hpp - Debug-info engine for the udap DAP debugger.
//
// Port of udap's dbg class (https://github.com/retro-vault/udap) onto the
// partner machine: instead of owning a z80ex CPU and a flat memory vector,
// all CPU/memory state comes from the live partner emulator. Holds the
// SDCC CDB/MAP debug info, the source index, breakpoints, the full 64 KB
// disassembly listing and the step-back history.
#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <sdcc/cdbg_info.h>
#include <sdcc/segment.h>
#include <sdcc/symbol.h>

#include "../partner.hpp"

struct dap_source_location {
    std::string file;
    int line = 0;
    bool is_asm = false;
};

class dap_dbg
{
public:
    explicit dap_dbg(partner &emu) : emu_(&emu) {}

    partner &emu() { return *emu_; }
    const partner &emu() const { return *emu_; }

    // Breakpoint management.
    std::vector<uint16_t> &instruction_breakpoints() { return instruction_breakpoints_; }
    std::vector<uint16_t> &function_breakpoints() { return function_breakpoints_; }
    void set_source_breakpoints_for_file(const std::string &file, std::vector<int> lines);
    struct resolved_breakpoint {
        bool verified = false;
        int line = 0;
        std::string message;
    };
    std::vector<resolved_breakpoint> resolve_source_breakpoints_for_file(const std::string &file) const;
    void rebuild_source_breakpoint_addresses();
    void rebuild_all_breakpoints();
    void set_asm_breakpoints(std::vector<uint16_t> addrs);
    bool is_breakpoint(uint16_t pc) const;

    // Virtual listing path served to the client.
    const std::string &virtual_lst_path() const { return virtual_lst_path_; }
    void set_virtual_lst_path(const std::string &p) { virtual_lst_path_ = p; }

    // CDB debug info.
    void set_cdb_modules(std::vector<sdcc::cdbg_info_module> m);
    const std::vector<sdcc::cdbg_info_module> &cdb_modules() const { return cdb_modules_; }
    bool has_cdb() const { return !cdb_modules_.empty(); }

    // Source roots for path resolution.
    void set_source_root(const std::string &r) { source_root_ = r; }
    void set_source_roots(std::vector<std::string> roots) { source_roots_ = std::move(roots); }

    // MAP info.
    void set_map_symbols(std::vector<sdcc::symbol> symbols);
    const std::vector<sdcc::symbol> &map_symbols() const { return map_symbols_; }
    void set_map_segments(std::vector<sdcc::segment> segments) { map_segments_ = std::move(segments); }
    const std::vector<sdcc::segment> &map_segments() const { return map_segments_; }
    bool has_map() const { return !map_symbols_.empty() || !map_segments_.empty(); }

    // C variable support.
    const sdcc::cdbg_info_function *lookup_function_at(uint16_t address) const;
    std::string c_variable_value(const sdcc::cdbg_info_symbol &sym) const;

    // Step-back history (one snapshot per user step press).
    void push_history();
    void pop_history();
    bool can_step_back() const { return !history_.empty(); }

    // Source / symbol lookups (O(1) after index is built).
    std::optional<dap_source_location> lookup_source(uint16_t address) const;     // C only
    std::optional<dap_source_location> lookup_source_any(uint16_t address) const; // C, then asm
    std::optional<uint16_t> lookup_address(const std::string &file, int line) const;
    std::optional<std::string> lookup_symbol_exact(uint16_t address) const;
    std::optional<std::string> lookup_symbol(uint16_t address) const;
    std::optional<uint16_t> lookup_function_address(const std::string &name) const;
    std::optional<dap_source_location> map_symbol_to_source(const sdcc::symbol &sym) const;
    std::optional<std::string> resolve_source_path(const std::string &path) const;

    // Full 64 KB disassembly listing - built once at launch, stable for the
    // whole session.
    void build_full_listing();
    void clear_full_listing();
    bool has_full_listing() const { return full_listing_built_; }
    const std::string &full_listing_content() const { return full_listing_content_; }
    const std::vector<uint16_t> &full_listing_addrs() const { return full_listing_addrs_; }
    int full_listing_line_for_addr(uint16_t addr) const;

    std::string symbolize_asm_line(const std::string &mnemonic) const;

    static std::string format_hex(uint16_t value, int width);

private:
    void rebuild_source_index();

    partner *emu_;

    // Breakpoints.
    std::vector<uint16_t> instruction_breakpoints_;
    std::vector<uint16_t> function_breakpoints_;
    std::unordered_map<std::string, std::vector<int>> source_breakpoints_by_file_;
    std::vector<uint16_t> source_breakpoint_addresses_;
    std::unordered_set<uint16_t> asm_breakpoints_;
    // Merged fast-lookup set (all lists combined).
    std::unordered_set<uint16_t> all_breakpoints_;

    // Virtual listing.
    std::string virtual_lst_path_ = "/__virtual__/listing.asm";

    // CDB / MAP debug info.
    std::vector<sdcc::cdbg_info_module> cdb_modules_;
    std::string source_root_;
    std::vector<std::string> source_roots_;
    std::vector<sdcc::symbol> map_symbols_;
    std::vector<sdcc::segment> map_segments_;

    // Step-back history. Note: partner's debug state covers the main register
    // set; alternate registers are restored only as far as the machine exposes
    // them through apply_debug_cpu_state.
    struct machine_snapshot {
        partner::debug_cpu_state regs;
        std::vector<uint8_t> memory;
    };
    std::deque<machine_snapshot> history_;
    static constexpr size_t kMaxHistory = 256;

    // Address-indexed source lookup (built by rebuild_source_index).
    std::unordered_map<uint16_t, dap_source_location> source_by_addr_; // C lines only
    std::unordered_map<uint16_t, dap_source_location> asm_by_addr_;    // asm lines only
    // key = "<filename>:<line>"
    std::unordered_map<std::string, uint16_t> addr_by_file_line_;

    // Full 64 KB listing (built once per launch).
    bool full_listing_built_ = false;
    std::string full_listing_content_;
    std::vector<uint16_t> full_listing_addrs_; // line_addrs[i] = address of line i+1
    std::unordered_map<uint16_t, int> full_listing_addr_to_line_;
};
