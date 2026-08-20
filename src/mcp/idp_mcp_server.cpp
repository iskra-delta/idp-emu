#include "mcp/idp_mcp_server.hpp"

#include "partner.hpp"
#include "partner_crt.hpp"
#include "partner_gdp.hpp"

#include <png.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace {
using json = nlohmann::json;

constexpr uint64_t clock_hz = 4000000;
constexpr uint64_t nominal_frame_rate = 60;
constexpr uint64_t max_ticks_per_call = 70000000;

json object_schema(json properties = json::object(),
                   json required = json::array())
{
    json schema = {{"type", "object"},
                   {"properties", std::move(properties)},
                   {"additionalProperties", false}};
    if (!required.empty())
        schema["required"] = std::move(required);
    return schema;
}

json tool(const char *name, const char *description, json schema)
{
    return {{"name", name}, {"description", description},
            {"inputSchema", std::move(schema)}};
}

json tool_result(std::string text, json structured = nullptr,
                 bool is_error = false)
{
    json result = {{"content", json::array({
        {{"type", "text"}, {"text", std::move(text)}}})}};
    if (!structured.is_null()) result["structuredContent"] = std::move(structured);
    if (is_error) result["isError"] = true;
    return result;
}

json image_tool_result(std::string text, std::string data, json structured)
{
    return {
        {"content", json::array({
            {{"type", "text"}, {"text", std::move(text)}},
            {{"type", "image"}, {"data", std::move(data)},
             {"mimeType", "image/png"}}})},
        {"structuredContent", std::move(structured)}};
}

json rpc_response(const json &id, json result)
{
    return {{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(result)}};
}

json rpc_error(const json &id, int code, std::string message)
{
    return {{"jsonrpc", "2.0"}, {"id", id},
            {"error", {{"code", code}, {"message", std::move(message)}}}};
}

uint64_t parse_unsigned(const json &value, const char *name)
{
    if (value.is_number_unsigned()) return value.get<uint64_t>();
    if (value.is_number_integer()) {
        const int64_t number = value.get<int64_t>();
        if (number < 0)
            throw std::invalid_argument(std::string("'") + name + "' is out of range");
        return (uint64_t)number;
    }
    if (value.is_string()) {
        std::string input = value.get<std::string>();
        input.erase(input.begin(), std::find_if(input.begin(), input.end(),
            [](unsigned char ch) { return !std::isspace(ch); }));
        input.erase(std::find_if(input.rbegin(), input.rend(),
            [](unsigned char ch) { return !std::isspace(ch); }).base(), input.end());
        int base = 10;
        size_t prefix = 0;
        if (input.size() > 2 && input[0] == '0' &&
            (input[1] == 'x' || input[1] == 'X')) {
            base = 16; prefix = 2;
        } else if (!input.empty() && (input[0] == '$' || input[0] == '#')) {
            base = 16; prefix = 1;
        }
        size_t used = 0;
        uint64_t number = 0;
        try {
            if (prefix == input.size()) throw std::invalid_argument("empty");
            number = std::stoull(input.substr(prefix), &used, base);
        } catch (const std::exception &) {
            throw std::invalid_argument(std::string("'") + name + "' is not a number");
        }
        if (used != input.size() - prefix)
            throw std::invalid_argument(std::string("'") + name + "' is not a number");
        return number;
    }
    throw std::invalid_argument(std::string("'") + name +
                                "' must be an integer or numeric string");
}

uint64_t unsigned_arg(const json &args, const char *name, uint64_t low,
                      uint64_t high, uint64_t fallback)
{
    if (!args.contains(name)) return fallback;
    const uint64_t number = parse_unsigned(args.at(name), name);
    if (number < low || number > high)
        throw std::invalid_argument(std::string("'") + name + "' is out of range");
    return number;
}

bool bool_arg(const json &args, const char *name, bool fallback)
{
    if (!args.contains(name)) return fallback;
    if (!args.at(name).is_boolean())
        throw std::invalid_argument(std::string("'") + name + "' must be boolean");
    return args.at(name).get<bool>();
}

void set_u16(const json &args, const char *name, uint16_t &field, bool &changed)
{
    if (!args.contains(name)) return;
    field = (uint16_t)unsigned_arg(args, name, 0, 0xFFFF, field);
    changed = true;
}

void set_u8(const json &args, const char *name, uint8_t &field, bool &changed,
            uint8_t high = 0xFF)
{
    if (!args.contains(name)) return;
    field = (uint8_t)unsigned_arg(args, name, 0, high, field);
    changed = true;
}

std::string hex_bytes(const std::vector<uint8_t> &bytes)
{
    std::ostringstream out;
    out << std::hex << std::uppercase << std::setfill('0');
    for (size_t i = 0; i < bytes.size(); ++i) {
        if (i) out << ' ';
        out << std::setw(2) << (unsigned)bytes[i];
    }
    return out.str();
}

std::string hex16(uint16_t value)
{
    std::ostringstream out;
    out << "0x" << std::hex << std::uppercase << std::setfill('0')
        << std::setw(4) << (unsigned)value;
    return out.str();
}

std::string flags_text(uint8_t flags)
{
    std::string out = "SZYHXPNC";
    for (int bit = 7; bit >= 0; --bit)
        if ((flags & (1u << bit)) == 0)
            out[7 - bit] = (char)std::tolower((unsigned char)out[7 - bit]);
    return out;
}

int hex_digit(unsigned char ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

std::vector<uint8_t> parse_bytes(const json &value, size_t maximum = 65536)
{
    std::vector<uint8_t> bytes;
    if (value.is_array()) {
        if (value.empty() || value.size() > maximum)
            throw std::invalid_argument("'data' has an invalid length");
        bytes.reserve(value.size());
        for (const json &item : value) {
            const uint64_t byte = parse_unsigned(item, "data item");
            if (byte > 255)
                throw std::invalid_argument("every data item must be a byte");
            bytes.push_back((uint8_t)byte);
        }
        return bytes;
    }
    if (!value.is_string())
        throw std::invalid_argument("'data' must be hex text or a byte array");
    std::string digits;
    for (unsigned char ch : value.get<std::string>()) {
        if (std::isspace(ch)) continue;
        if (hex_digit(ch) < 0)
            throw std::invalid_argument("hex data contains a non-hex character");
        digits.push_back((char)ch);
    }
    if (digits.empty() || (digits.size() & 1u))
        throw std::invalid_argument("hex data must contain complete byte pairs");
    if (digits.size() / 2 > maximum)
        throw std::invalid_argument("'data' is too long");
    bytes.reserve(digits.size() / 2);
    for (size_t i = 0; i < digits.size(); i += 2)
        bytes.push_back((uint8_t)((hex_digit(digits[i]) << 4) |
                                  hex_digit(digits[i + 1])));
    return bytes;
}

std::vector<uint8_t> read_file(const std::string &path, size_t maximum)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open file: " + path);
    input.seekg(0, std::ios::end);
    const std::streamoff size = input.tellg();
    if (size < 0 || (uint64_t)size > maximum)
        throw std::runtime_error("file is too large: " + path);
    input.seekg(0);
    std::vector<uint8_t> data((size_t)size);
    if (!data.empty()) input.read(reinterpret_cast<char *>(data.data()), size);
    if (!input && !data.empty())
        throw std::runtime_error("could not read complete file: " + path);
    return data;
}

std::string base64_encode(const std::vector<uint8_t> &data)
{
    static constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);
    for (size_t i = 0; i < data.size(); i += 3) {
        const uint32_t a = data[i];
        const uint32_t b = i + 1 < data.size() ? data[i + 1] : 0;
        const uint32_t c = i + 2 < data.size() ? data[i + 2] : 0;
        const uint32_t value = (a << 16) | (b << 8) | c;
        out.push_back(alphabet[(value >> 18) & 63]);
        out.push_back(alphabet[(value >> 12) & 63]);
        out.push_back(i + 1 < data.size() ? alphabet[(value >> 6) & 63] : '=');
        out.push_back(i + 2 < data.size() ? alphabet[value & 63] : '=');
    }
    return out;
}

std::vector<uint8_t> encode_png(const std::vector<uint8_t> &rgb,
                                int width, int height)
{
    png_image image{};
    image.version = PNG_IMAGE_VERSION;
    image.width = (png_uint_32)width;
    image.height = (png_uint_32)height;
    image.format = PNG_FORMAT_RGB;
    png_alloc_size_t size = 0;
    if (!png_image_write_to_memory(&image, nullptr, &size, 0, rgb.data(), 0, nullptr))
        throw std::runtime_error("PNG encoder could not size the image");
    std::vector<uint8_t> png((size_t)size);
    if (!png_image_write_to_memory(&image, png.data(), &size, 0, rgb.data(), 0, nullptr))
        throw std::runtime_error(std::string("PNG encoding failed: ") + image.message);
    png.resize((size_t)size);
    return png;
}

std::string lower_extension(const std::string &path)
{
    std::string ext = std::filesystem::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char ch) { return (char)std::tolower(ch); });
    return ext;
}

std::string trim_terminal_text(const std::string &input)
{
    std::istringstream in(input);
    std::ostringstream out;
    std::string line;
    bool first = true;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == ' ' || line.back() == '\r'))
            line.pop_back();
        if (!first) out << '\n';
        out << line;
        first = false;
    }
    std::string result = out.str();
    while (!result.empty() && result.back() == '\n') result.pop_back();
    return result;
}
} // namespace

idp_mcp_server::idp_mcp_server(partner &machine, std::string model)
    : machine_(machine), model_(std::move(model))
{
    framebuffer_.set_phosphor_type(display::phosphor_type::flat);
    framebuffer_.load_font("");
}

json idp_mcp_server::list_tools() const
{
    const json word = {{"type", "integer"}, {"minimum", 0}, {"maximum", 65535}};
    const json byte = {{"type", "integer"}, {"minimum", 0}, {"maximum", 255}};
    const json data = {{"oneOf", json::array({
        json{{"type", "string"}},
        json{{"type", "array"}, {"items", byte}}})}};
    json tools = json::array();
    tools.push_back(tool("load",
        "Load raw binary or a Partner ROM, or mount a floppy/hard-disk image. Supply path or inline hex/byte data. Formats: binary, rom, fd0-fd3 and hdd.",
        object_schema({{"path", {{"type", "string"}}}, {"data", data},
            {"format", {{"type", "string"}, {"enum", {"binary", "rom", "fd0", "fd1", "fd2", "fd3", "hdd"}}}},
            {"address", word}, {"start", word}, {"reset", {{"type", "boolean"}}}})));
    tools.push_back(tool("reset",
        "Reset every emulated chip. RAM is retained like hardware unless clear_memory is true; mounted media remains attached.",
        object_schema({{"clear_memory", {{"type", "boolean"}}}})));
    tools.push_back(tool("run",
        "Run to a tick/T-state, nominal 60 Hz frame, instruction, address, HALT, or signal breakpoint limit. One tick is one 4 MHz clock.",
        object_schema({
            {"frames", {{"type", "integer"}, {"minimum", 1}, {"maximum", 1000}}},
            {"tstates", {{"type", "integer"}, {"minimum", 1}, {"maximum", max_ticks_per_call}}},
            {"ticks", {{"type", "integer"}, {"minimum", 1}, {"maximum", max_ticks_per_call}}},
            {"instructions", {{"type", "integer"}, {"minimum", 1}, {"maximum", 100000000}}},
            {"until_pc", word},
            {"max_ticks", {{"type", "integer"}, {"minimum", 1}, {"maximum", max_ticks_per_call}}},
            {"stop_on_halt", {{"type", "boolean"}}}})));
    tools.push_back(tool("run_until",
        "Run until PC reaches address between instructions, a breakpoint fires, or max_tstates expires.",
        object_schema({{"address", word},
            {"max_tstates", {{"type", "integer"}, {"minimum", 1}, {"maximum", max_ticks_per_call}}},
            {"stop_on_halt", {{"type", "boolean"}}}}, {"address"})));
    tools.push_back(tool("step",
        "Execute complete Z80 instructions (one by default) and report exact chip clocks consumed.",
        object_schema({{"count", {{"type", "integer"}, {"minimum", 1}, {"maximum", 1000000}}}})));
    tools.push_back(tool("measure_cycles",
        "Measure exact 4 MHz chip clocks/T-states for instructions or a routine ending at until_pc. Optional address redirects execution first.",
        object_schema({{"address", word},
            {"instructions", {{"type", "integer"}, {"minimum", 1}, {"maximum", 1000000}}},
            {"until_pc", word},
            {"max_ticks", {{"type", "integer"}, {"minimum", 1}, {"maximum", max_ticks_per_call}}},
            {"stop_on_halt", {{"type", "boolean"}}}})));
    tools.push_back(tool("status",
        "Inspect configuration, the 4 MHz timing counter, CPU, DMA, FDC and chip-bus state without advancing time.", object_schema()));

    json registers = {{"af", word}, {"bc", word}, {"de", word}, {"hl", word},
        {"ix", word}, {"iy", word}, {"sp", word}, {"pc", word},
        {"af_alt", word}, {"bc_alt", word}, {"de_alt", word}, {"hl_alt", word},
        {"i", byte}, {"r", byte},
        {"im", {{"type", "integer"}, {"minimum", 0}, {"maximum", 2}}},
        {"iff1", {{"type", "boolean"}}}, {"iff2", {{"type", "boolean"}}}};
    tools.push_back(tool("registers",
        "Read or update the complete Z80 register file, including shadow registers and interrupt mode.",
        object_schema(std::move(registers))));
    tools.push_back(tool("read_memory",
        "Read CPU-visible bytes without time passing. Addresses wrap at 0xFFFF; 0x, $ and # numeric strings are accepted.",
        object_schema({{"address", word},
            {"length", {{"type", "integer"}, {"minimum", 1}, {"maximum", 4096}}}}, {"address"})));
    tools.push_back(tool("write_memory",
        "Write hex text or a byte array without time passing. allow_rom patches the visible mirrored 2K ROM.",
        object_schema({{"address", word}, {"data", data},
            {"allow_rom", {{"type", "boolean"}}}}, {"address", "data"})));
    tools.push_back(tool("breakpoint",
        "Manage execute, memory and I/O signal breakpoints: add, remove, list, clear, enable or disable.",
        object_schema({
            {"action", {{"type", "string"}, {"enum", {"add", "remove", "list", "clear", "enable", "disable"}}}},
            {"kind", {{"type", "string"}, {"enum", {"execute", "memory_read", "memory_write", "io_read", "io_write"}}}},
            {"address", word}, {"value", byte},
            {"id", {{"type", "integer"}, {"minimum", 1}, {"maximum", 1000000}}}})));
    tools.push_back(tool("press_keys",
        "Type text or named serial keys through the Partner keyboard SIO and run frames so firmware can consume them.",
        object_schema({{"text", {{"type", "string"}, {"maxLength", 4096}}},
            {"keys", {{"type", "array"}, {"items", {{"type", "string"}}}}},
            {"hold_frames", {{"type", "integer"}, {"minimum", 1}, {"maximum", 100}}},
            {"gap_frames", {{"type", "integer"}, {"minimum", 0}, {"maximum", 100}}}})));
    tools.push_back(tool("read_port",
        "Perform a real motherboard I/O read without running the CPU; it may acknowledge or consume chip state.",
        object_schema({{"port", word}}, {"port"})));
    tools.push_back(tool("set_port",
        "Perform a real motherboard I/O write without running the CPU.",
        object_schema({{"port", word}, {"value", byte}}, {"port", "value"})));
    tools.push_back(tool("read_io", "Alias of read_port for existing Partner MCP clients.",
        object_schema({{"port", word}}, {"port"})));
    tools.push_back(tool("write_io", "Alias of set_port for existing Partner MCP clients.",
        object_schema({{"port", word}, {"value", byte}}, {"port", "value"})));
    tools.push_back(tool("keyboard",
        "Queue raw text bytes on the Partner keyboard SIO; optional ticks_per_byte advances real chip time.",
        object_schema({{"text", {{"type", "string"}, {"maxLength", 4096}}},
            {"ticks_per_byte", {{"type", "integer"}, {"minimum", 0}, {"maximum", clock_hz}}}}, {"text"})));
    tools.push_back(tool("screen",
        "Return the current chip-rendered CRT/GDP raster as a PNG MCP image without opening a window.",
        object_schema({{"include_border", {{"type", "boolean"}}},
            {"scale", {{"type", "integer"}, {"minimum", 1}, {"maximum", 4}}}})));
    tools.push_back(tool("screen_text",
        "Read terminal characters or render the chip framebuffer as ASCII art; serial and printer transcripts accompany chars mode.",
        object_schema({
            {"mode", {{"type", "string"}, {"enum", {"chars", "ascii"}}}},
            {"font_address", word},
            {"columns", {{"type", "integer"}, {"minimum", 8}, {"maximum", 200}}},
            {"rows", {{"type", "integer"}, {"minimum", 4}, {"maximum", 100}}},
            {"trim", {{"type", "boolean"}}}})));
    tools.push_back(tool("screenshot",
        "Save the chip-rendered raster to a PNG file, replacing that path.",
        object_schema({{"path", {{"type", "string"}, {"minLength", 1}}},
            {"include_border", {{"type", "boolean"}}},
            {"scale", {{"type", "integer"}, {"minimum", 1}, {"maximum", 4}}}}, {"path"})));
    tools.push_back(tool("video_start",
        "Start YUV4MPEG2 recording of the chip-rendered screen at nominal 60 Hz; frames advance only while emulation runs.",
        object_schema({{"path", {{"type", "string"}, {"minLength", 1}}},
            {"include_border", {{"type", "boolean"}}}}, {"path"})));
    tools.push_back(tool("video_stop",
        "Stop YUV4MPEG2 recording and report path, frame count, duration and byte size.", object_schema()));
    tools.push_back(tool("mount_media",
        "Attach a Partner floppy or SASI hard-disk image from the server filesystem.",
        object_schema({
            {"kind", {{"type", "string"}, {"enum", {"fd0", "fd1", "fd2", "fd3", "hdd"}}}},
            {"path", {{"type", "string"}, {"minLength", 1}}}}, {"kind", "path"})));
    return tools;
}

json idp_mcp_server::register_state() const
{
    const auto state = machine_.capture_debug_cpu_state();
    return {
        {"af", state.af}, {"bc", state.bc}, {"de", state.de}, {"hl", state.hl},
        {"ix", state.ix}, {"iy", state.iy}, {"sp", state.sp},
        {"pc", machine_.get_current_pc()},
        {"af_alt", state.af_alt}, {"bc_alt", state.bc_alt},
        {"de_alt", state.de_alt}, {"hl_alt", state.hl_alt},
        {"i", state.i}, {"r", state.r}, {"im", state.im},
        {"iff1", state.iff1}, {"iff2", state.iff2},
        {"halted", state.halted}, {"flags", flags_text((uint8_t)state.af)}};
}

json idp_mcp_server::machine_state() const
{
    const auto &dma = machine_.get_dma();
    const auto &fdc = machine_.get_fdc();
    return {
        {"model", model_}, {"clock_hz", clock_hz}, {"cpu_hz", clock_hz},
        {"cycle_unit", "one 4 MHz master/CPU clock (Z80 T-state)"},
        {"nominal_video_hz", nominal_frame_rate},
        {"ticks", machine_.get_tick_count()},
        {"tstates", machine_.get_tick_count()},
        {"cycles", machine_.get_tick_count()},
        {"emulated_seconds", (double)machine_.get_tick_count() / (double)clock_hz},
        {"rom_enabled", machine_.is_rom_enabled()},
        {"ram_bank", machine_.get_ram_bank()},
        {"bus_pins", machine_.get_pins()}, {"cpu", register_state()},
        {"dma", {{"enabled", dma.enabled}, {"state", (int)dma.state},
                  {"bus_request", (machine_.get_pins() & Z80DMA_BUSREQ) != 0}}},
        {"fdc", {{"phase", (int)fdc.phase}, {"irq", fdc.irq_request},
                  {"motor", machine_.get_fdc_motor()},
                  {"vector", machine_.get_fdc_int_vector()}}},
        {"breakpoints", breakpoints_.size()},
        {"video_recording", video_file_.is_open()}};
}

json idp_mcp_server::breakpoint_state() const
{
    json entries = json::array();
    for (const auto &bp : breakpoints_) {
        json item = {{"id", bp.id}, {"kind", bp.kind},
                     {"address", bp.address}, {"enabled", bp.enabled},
                     {"hits", bp.hits}};
        if (bp.value) item["value"] = *bp.value;
        entries.push_back(std::move(item));
    }
    return {{"breakpoints", std::move(entries)}};
}

std::optional<uint32_t> idp_mcp_server::execute_breakpoint(uint16_t address)
{
    for (auto &bp : breakpoints_) {
        if (bp.enabled && bp.kind == "execute" && bp.address == address) {
            ++bp.hits;
            return bp.id;
        }
    }
    return std::nullopt;
}

std::optional<uint32_t> idp_mcp_server::bus_breakpoint(uint64_t pins)
{
    std::string kind;
    if ((pins & (Z80_MREQ | Z80_RD)) == (Z80_MREQ | Z80_RD))
        kind = "memory_read";
    else if ((pins & (Z80_MREQ | Z80_WR)) == (Z80_MREQ | Z80_WR))
        kind = "memory_write";
    else if ((pins & (Z80_IORQ | Z80_RD)) == (Z80_IORQ | Z80_RD) &&
             (pins & Z80_M1) == 0)
        kind = "io_read";
    else if ((pins & (Z80_IORQ | Z80_WR)) == (Z80_IORQ | Z80_WR))
        kind = "io_write";
    else
        return std::nullopt;

    const uint16_t address = Z80_GET_ADDR(pins);
    const uint8_t value = Z80_GET_DATA(pins);
    for (auto &bp : breakpoints_) {
        if (bp.enabled && bp.kind == kind && bp.address == address &&
            (!bp.value || *bp.value == value)) {
            ++bp.hits;
            return bp.id;
        }
    }
    return std::nullopt;
}

json idp_mcp_server::run_machine(uint64_t tick_limit,
                                 uint64_t instruction_limit,
                                 std::optional<uint16_t> until_pc,
                                 bool stop_on_halt)
{
    const uint64_t start_tick = machine_.get_tick_count();
    const uint16_t start_pc = machine_.get_current_pc();
    uint64_t ticks = 0;
    uint64_t instructions = 0;
    std::string reason = (instruction_limit || until_pc) ? "max_ticks" : "completed";
    std::optional<uint32_t> hit;
    std::optional<uint32_t> pending_bus_hit;
    bool previous_boundary = machine_.is_opdone();

    while (ticks < tick_limit) {
        const bool boundary = machine_.is_opdone();
        if (pending_bus_hit && boundary) {
            hit = pending_bus_hit;
            reason = "breakpoint";
            break;
        }
        if (until_pc && (boundary || ticks == 0) &&
            machine_.get_current_pc() == *until_pc) {
            reason = "address_reached";
            break;
        }
        if (instruction_limit && instructions >= instruction_limit) {
            reason = "instruction_limit";
            break;
        }
        if (boundary || ticks == 0) {
            hit = execute_breakpoint(machine_.get_current_pc());
            if (hit) {
                reason = "breakpoint";
                break;
            }
        }
        machine_.tick();
        ++ticks;
        capture_video_if_due();
        if (!pending_bus_hit)
            pending_bus_hit = bus_breakpoint(machine_.get_last_cpu_bus_pins());
        const bool now_boundary = machine_.is_opdone();
        if (now_boundary && !previous_boundary) ++instructions;
        previous_boundary = now_boundary;
        if (stop_on_halt && machine_.capture_debug_cpu_state().halted) {
            reason = "halted";
            break;
        }
    }

    json result = {
        {"reason", reason}, {"start_tick", start_tick},
        {"end_tick", machine_.get_tick_count()}, {"start_pc", start_pc},
        {"pc", machine_.get_current_pc()}, {"ticks", ticks},
        {"tstates", ticks}, {"cycles", ticks}, {"instructions", instructions},
        {"frames", (double)ticks * (double)nominal_frame_rate / (double)clock_hz},
        {"seconds", (double)ticks / (double)clock_hz},
        {"clock_hz", clock_hz}, {"registers", register_state()}};
    if (hit) result["breakpoint_id"] = *hit;
    return result;
}

idp_mcp_server::captured_screen idp_mcp_server::capture_screen(int scale)
{
    framebuffer_.set_phosphor_type(display::phosphor_type::flat);
    if (auto *crt = dynamic_cast<partner_crt *>(&machine_))
        crt->render_to(framebuffer_);
    else if (auto *gdp = dynamic_cast<partner_gdp *>(&machine_))
        gdp->render_to(framebuffer_);

    const int source_width = framebuffer_.content_width();
    const int source_height = framebuffer_.content_height();
    captured_screen result;
    result.width = source_width * scale;
    result.height = source_height * scale;
    result.rgb.resize((size_t)result.width * (size_t)result.height * 3);
    const uint8_t *source = framebuffer_.data();
    for (int y = 0; y < result.height; ++y) {
        const int sy = y / scale;
        for (int x = 0; x < result.width; ++x) {
            const int sx = x / scale;
            const uint8_t code = source[sy * display::FB_W + sx];
            uint8_t r = code, g = code, b = code;
            if (code >= 0xF0) {
                const uint8_t index = code & 0x0F;
                const uint8_t high = (index & 8) ? 255 : 184;
                r = (index & 4) ? high : 0;
                g = (index & 2) ? high : 0;
                b = (index & 1) ? high : 0;
            }
            const size_t offset = ((size_t)y * result.width + x) * 3;
            result.rgb[offset] = r;
            result.rgb[offset + 1] = g;
            result.rgb[offset + 2] = b;
        }
    }
    return result;
}

void idp_mcp_server::record_video_frame()
{
    if (!video_file_.is_open()) return;
    const captured_screen frame = capture_screen();
    if (frame.width != video_width_ || frame.height != video_height_)
        throw std::runtime_error("screen dimensions changed during recording");
    const size_t pixels = (size_t)frame.width * frame.height;
    std::vector<uint8_t> y(pixels), u(pixels), v(pixels);
    auto clamp_byte = [](int value) { return (uint8_t)std::clamp(value, 0, 255); };
    for (size_t i = 0; i < pixels; ++i) {
        const int r = frame.rgb[i * 3];
        const int g = frame.rgb[i * 3 + 1];
        const int b = frame.rgb[i * 3 + 2];
        y[i] = clamp_byte(16 + ((66 * r + 129 * g + 25 * b + 128) >> 8));
        u[i] = clamp_byte(128 + ((-38 * r - 74 * g + 112 * b + 128) >> 8));
        v[i] = clamp_byte(128 + ((112 * r - 94 * g - 18 * b + 128) >> 8));
    }
    video_file_ << "FRAME\n";
    video_file_.write(reinterpret_cast<const char *>(y.data()), (std::streamsize)y.size());
    video_file_.write(reinterpret_cast<const char *>(u.data()), (std::streamsize)u.size());
    video_file_.write(reinterpret_cast<const char *>(v.data()), (std::streamsize)v.size());
    if (!video_file_)
        throw std::runtime_error("could not write video frame to " + video_path_);
    ++video_frames_;
}

void idp_mcp_server::capture_video_if_due()
{
    if (!video_file_.is_open()) return;
    const uint64_t next_tick = video_start_tick_ +
        ((video_frames_ + 1) * clock_hz + nominal_frame_rate - 1) /
            nominal_frame_rate;
    if (machine_.get_tick_count() >= next_tick) record_video_frame();
}

json idp_mcp_server::invoke_tool(const std::string &name, const json &arguments)
{
    try {
        if (!arguments.is_object())
            throw std::invalid_argument("tool arguments must be an object");

        if (name == "status") {
            const json state = machine_state();
            return tool_result("Partner " + model_ + " at PC " +
                hex16(machine_.get_current_pc()) + " after " +
                std::to_string(machine_.get_tick_count()) + " cycles (4 MHz)", state);
        }
        if (name == "reset") {
            const bool clear = bool_arg(arguments, "clear_memory", false);
            machine_.reset();
            if (clear) machine_.clear_debug_memory();
            return tool_result(clear ? "machine reset and RAM cleared" : "machine reset",
                               machine_state());
        }
        if (name == "load") {
            const bool has_path = arguments.contains("path");
            const bool has_data = arguments.contains("data");
            if (has_path == has_data)
                throw std::invalid_argument("give exactly one of 'path' or 'data'");
            if (has_path && !arguments["path"].is_string())
                throw std::invalid_argument("'path' must be a string");
            const std::string path = has_path ? arguments["path"].get<std::string>() : "";
            std::string format;
            if (arguments.contains("format")) {
                if (!arguments["format"].is_string())
                    throw std::invalid_argument("'format' must be a string");
                format = arguments["format"].get<std::string>();
            } else {
                format = has_path && lower_extension(path) == ".rom" ? "rom" : "binary";
            }
            const std::array<std::string, 7> formats =
                {"binary", "rom", "fd0", "fd1", "fd2", "fd3", "hdd"};
            if (std::find(formats.begin(), formats.end(), format) == formats.end())
                throw std::invalid_argument("unsupported Partner load format '" + format + "'");
            if (format.rfind("fd", 0) == 0 || format == "hdd") {
                if (!has_path) throw std::invalid_argument("media formats require 'path'");
                if (format == "hdd") machine_.load_hdd(path);
                else machine_.load_disk(format[2] - '0', path);
                return tool_result("mounted " + format + " from " + path,
                                   {{"format", format}, {"path", path}});
            }
            std::vector<uint8_t> bytes = has_path
                ? read_file(path, 65536) : parse_bytes(arguments["data"], 65536);
            const bool reset = bool_arg(arguments, "reset", false);
            if (format == "rom") {
                machine_.load_debug_rom(bytes);
                if (reset) machine_.reset();
            } else {
                if (reset) {
                    machine_.reset();
                    machine_.clear_debug_memory();
                }
                const uint16_t address = (uint16_t)unsigned_arg(
                    arguments, "address", 0, 0xFFFF, 0x8000);
                machine_.write_debug_memory(address, bytes);
                if (arguments.contains("start"))
                    machine_.debug_set_pc((uint16_t)unsigned_arg(
                        arguments, "start", 0, 0xFFFF, 0));
            }
            json result = {{"format", format}, {"bytes", bytes.size()},
                           {"reset", reset}};
            if (format == "binary")
                result["address"] = unsigned_arg(arguments, "address", 0, 0xFFFF, 0x8000);
            if (has_path) result["path"] = path;
            return tool_result("loaded " + std::to_string(bytes.size()) +
                               " bytes as " + format, std::move(result));
        }
        if (name == "registers") {
            auto state = machine_.capture_debug_cpu_state();
            bool changed = false;
            set_u16(arguments, "af", state.af, changed);
            set_u16(arguments, "bc", state.bc, changed);
            set_u16(arguments, "de", state.de, changed);
            set_u16(arguments, "hl", state.hl, changed);
            set_u16(arguments, "ix", state.ix, changed);
            set_u16(arguments, "iy", state.iy, changed);
            set_u16(arguments, "sp", state.sp, changed);
            set_u16(arguments, "af_alt", state.af_alt, changed);
            set_u16(arguments, "bc_alt", state.bc_alt, changed);
            set_u16(arguments, "de_alt", state.de_alt, changed);
            set_u16(arguments, "hl_alt", state.hl_alt, changed);
            set_u8(arguments, "i", state.i, changed);
            set_u8(arguments, "r", state.r, changed);
            set_u8(arguments, "im", state.im, changed, 2);
            if (arguments.contains("iff1")) {
                state.iff1 = bool_arg(arguments, "iff1", state.iff1); changed = true;
            }
            if (arguments.contains("iff2")) {
                state.iff2 = bool_arg(arguments, "iff2", state.iff2); changed = true;
            }
            if (changed) machine_.apply_debug_cpu_state(state);
            if (arguments.contains("pc"))
                machine_.debug_set_pc((uint16_t)unsigned_arg(
                    arguments, "pc", 0, 0xFFFF, machine_.get_current_pc()));
            const json now = register_state();
            return tool_result("PC=" + hex16(now["pc"].get<uint16_t>()) +
                " SP=" + hex16(now["sp"].get<uint16_t>()) +
                " AF=" + hex16(now["af"].get<uint16_t>()), now);
        }
        if (name == "run" || name == "run_until" || name == "step" ||
            name == "measure_cycles") {
            uint64_t budget = 0;
            uint64_t instruction_limit = 0;
            std::optional<uint16_t> until_pc;
            const bool stop_on_halt = bool_arg(arguments, "stop_on_halt", false);
            if (name == "run") {
                const int limits = (arguments.contains("frames") ? 1 : 0) +
                    (arguments.contains("tstates") ? 1 : 0) +
                    (arguments.contains("ticks") ? 1 : 0) +
                    (arguments.contains("instructions") ? 1 : 0) +
                    (arguments.contains("until_pc") ? 1 : 0);
                if (limits > 1) throw std::invalid_argument("give only one run limit");
                if (arguments.contains("frames")) {
                    const uint64_t frames = unsigned_arg(arguments, "frames", 1, 1000, 1);
                    budget = (frames * clock_hz + nominal_frame_rate - 1) /
                             nominal_frame_rate;
                } else if (arguments.contains("tstates")) {
                    budget = unsigned_arg(arguments, "tstates", 1, max_ticks_per_call, 0);
                } else if (arguments.contains("ticks")) {
                    budget = unsigned_arg(arguments, "ticks", 1, max_ticks_per_call, 0);
                } else if (arguments.contains("instructions")) {
                    instruction_limit = unsigned_arg(arguments, "instructions", 1,
                                                     100000000, 0);
                    budget = unsigned_arg(arguments, "max_ticks", 1,
                                          max_ticks_per_call, clock_hz);
                } else if (arguments.contains("until_pc")) {
                    until_pc = (uint16_t)unsigned_arg(arguments, "until_pc", 0, 0xFFFF, 0);
                    budget = unsigned_arg(arguments, "max_ticks", 1,
                                          max_ticks_per_call, clock_hz);
                } else {
                    budget = (clock_hz + nominal_frame_rate - 1) / nominal_frame_rate;
                }
            } else if (name == "run_until") {
                until_pc = (uint16_t)unsigned_arg(arguments, "address", 0, 0xFFFF, 0);
                budget = unsigned_arg(arguments, "max_tstates", 1,
                                      max_ticks_per_call, clock_hz);
            } else if (name == "step") {
                instruction_limit = unsigned_arg(arguments, "count", 1, 1000000, 1);
                budget = max_ticks_per_call;
            } else {
                if (arguments.contains("address"))
                    machine_.debug_set_pc((uint16_t)unsigned_arg(
                        arguments, "address", 0, 0xFFFF, 0));
                if (arguments.contains("instructions") && arguments.contains("until_pc"))
                    throw std::invalid_argument("give only 'instructions' or 'until_pc'");
                if (arguments.contains("until_pc"))
                    until_pc = (uint16_t)unsigned_arg(arguments, "until_pc", 0, 0xFFFF, 0);
                else
                    instruction_limit = unsigned_arg(arguments, "instructions", 1,
                                                     1000000, 1);
                budget = unsigned_arg(arguments, "max_ticks", 1,
                                      max_ticks_per_call, clock_hz);
            }
            json timing = run_machine(budget, instruction_limit, until_pc, stop_on_halt);
            if (name == "run_until" && timing["reason"] == "max_ticks")
                timing["reason"] = "completed";
            if (name == "measure_cycles") {
                const uint64_t count = timing["instructions"].get<uint64_t>();
                timing["average_cycles"] = count
                    ? (double)timing["cycles"].get<uint64_t>() / (double)count : 0.0;
                timing["measurement"] = "elapsed 4 MHz chip clocks/T-states";
            }
            json result = machine_state();
            result[name == "measure_cycles" ? "measurement" : "run"] = timing;
            const std::string label = name == "measure_cycles" ? "measured " : "ran ";
            return tool_result(label + std::to_string(timing["cycles"].get<uint64_t>()) +
                " cycles and " + std::to_string(timing["instructions"].get<uint64_t>()) +
                " instructions; " + timing["reason"].get<std::string>(), std::move(result));
        }
        if (name == "read_memory") {
            const uint16_t address = (uint16_t)unsigned_arg(arguments, "address", 0, 0xFFFF, 0);
            const uint64_t length = unsigned_arg(arguments, "length", 1, 4096, 16);
            const auto bytes = machine_.read_debug_memory(address, (size_t)length);
            json data = json::array();
            for (uint8_t value : bytes) data.push_back(value);
            return tool_result(hex_bytes(bytes), {{"address", address}, {"length", length},
                                                   {"data", std::move(data)}});
        }
        if (name == "write_memory") {
            const uint16_t address = (uint16_t)unsigned_arg(arguments, "address", 0, 0xFFFF, 0);
            if (!arguments.contains("data")) throw std::invalid_argument("'data' is required");
            const auto bytes = parse_bytes(arguments["data"], 4096);
            const bool allow_rom = bool_arg(arguments, "allow_rom", false);
            machine_.write_debug_memory(address, bytes, allow_rom);
            return tool_result("wrote " + std::to_string(bytes.size()) + " bytes at " +
                hex16(address), {{"address", address}, {"length", bytes.size()},
                                 {"allow_rom", allow_rom}});
        }
        if (name == "breakpoint") {
            const std::string action = arguments.value("action", "list");
            if (action == "list")
                return tool_result(std::to_string(breakpoints_.size()) + " breakpoints",
                                   breakpoint_state());
            if (action == "clear") {
                const size_t count = breakpoints_.size();
                breakpoints_.clear();
                return tool_result("cleared " + std::to_string(count) + " breakpoints",
                                   breakpoint_state());
            }
            if (action == "add") {
                if (!arguments.contains("kind") || !arguments["kind"].is_string() ||
                    !arguments.contains("address"))
                    throw std::invalid_argument("adding requires 'kind' and 'address'");
                const std::string kind = arguments["kind"].get<std::string>();
                const std::array<std::string, 5> kinds = {"execute", "memory_read",
                    "memory_write", "io_read", "io_write"};
                if (std::find(kinds.begin(), kinds.end(), kind) == kinds.end())
                    throw std::invalid_argument("unknown breakpoint kind");
                if (next_breakpoint_id_ > 1000000)
                    throw std::runtime_error("breakpoint id space exhausted");
                breakpoint_entry entry;
                entry.id = next_breakpoint_id_++;
                entry.kind = kind;
                entry.address = (uint16_t)unsigned_arg(arguments, "address", 0, 0xFFFF, 0);
                if (arguments.contains("value") && kind != "execute")
                    entry.value = (uint8_t)unsigned_arg(arguments, "value", 0, 255, 0);
                const uint32_t id = entry.id;
                breakpoints_.push_back(std::move(entry));
                json state = breakpoint_state();
                state["id"] = id;
                return tool_result("added breakpoint " + std::to_string(id), std::move(state));
            }
            if (action == "remove" || action == "enable" || action == "disable") {
                const uint32_t id = (uint32_t)unsigned_arg(arguments, "id", 1, 1000000, 0);
                auto it = std::find_if(breakpoints_.begin(), breakpoints_.end(),
                    [id](const breakpoint_entry &bp) { return bp.id == id; });
                if (it == breakpoints_.end())
                    throw std::invalid_argument("no breakpoint with id " + std::to_string(id));
                if (action == "remove") breakpoints_.erase(it);
                else it->enabled = action == "enable";
                return tool_result(action + "d breakpoint " + std::to_string(id),
                                   breakpoint_state());
            }
            throw std::invalid_argument("unknown breakpoint action");
        }
        if (name == "read_io" || name == "read_port") {
            const uint16_t port = (uint16_t)unsigned_arg(arguments, "port", 0, 0xFFFF, 0);
            const uint8_t value = machine_.read_debug_io(port);
            return tool_result("port " + hex16(port) + " returned " +
                std::to_string((unsigned)value),
                {{"port", port}, {"value", value}, {"cycles", 0}});
        }
        if (name == "write_io" || name == "set_port") {
            const uint16_t port = (uint16_t)unsigned_arg(arguments, "port", 0, 0xFFFF, 0);
            const uint8_t value = (uint8_t)unsigned_arg(arguments, "value", 0, 255, 0);
            machine_.write_debug_io(port, value);
            return tool_result("wrote " + std::to_string((unsigned)value) + " to port " +
                hex16(port), {{"port", port}, {"value", value}, {"cycles", 0}});
        }
        if (name == "keyboard" || name == "press_keys") {
            std::string input;
            uint64_t spacing = 0;
            if (name == "keyboard") {
                if (!arguments.contains("text") || !arguments["text"].is_string())
                    throw std::invalid_argument("'text' must be a string");
                input = arguments["text"].get<std::string>();
                spacing = unsigned_arg(arguments, "ticks_per_byte", 0, clock_hz, 0);
            } else {
                const bool text_given = arguments.contains("text");
                const bool keys_given = arguments.contains("keys");
                if (text_given == keys_given)
                    throw std::invalid_argument("give exactly one of 'text' or 'keys'");
                if (text_given) {
                    if (!arguments["text"].is_string())
                        throw std::invalid_argument("'text' must be a string");
                    input = arguments["text"].get<std::string>();
                } else {
                    if (!arguments["keys"].is_array())
                        throw std::invalid_argument("'keys' must be an array");
                    for (const json &item : arguments["keys"]) {
                        if (!item.is_string())
                            throw std::invalid_argument("every key name must be a string");
                        std::string key = item.get<std::string>();
                        std::transform(key.begin(), key.end(), key.begin(),
                            [](unsigned char ch) { return (char)std::toupper(ch); });
                        if (key.size() == 1) input.push_back(key[0]);
                        else if (key == "ENTER") input.push_back('\r');
                        else if (key == "SPACE") input.push_back(' ');
                        else if (key == "TAB") input.push_back('\t');
                        else if (key == "BACKSPACE") input.push_back('\b');
                        else if (key == "ESC" || key == "ESCAPE") input.push_back('\x1B');
                        else throw std::invalid_argument(
                            "unsupported Partner serial key '" + key + "'");
                    }
                }
                const uint64_t hold = unsigned_arg(arguments, "hold_frames", 1, 100, 3);
                const uint64_t gap = unsigned_arg(arguments, "gap_frames", 0, 100, 2);
                spacing = ((hold + gap) * clock_hz + nominal_frame_rate - 1) /
                          nominal_frame_rate;
            }
            if (input.size() > 4096)
                throw std::invalid_argument("keyboard text is longer than 4096 bytes");
            if (spacing && input.size() > max_ticks_per_call / spacing)
                throw std::invalid_argument("key timing exceeds the per-call cycle limit");
            size_t accepted = 0;
            uint64_t elapsed = 0;
            for (unsigned char value : input) {
                bool ok = true;
                if (auto *crt = dynamic_cast<partner_crt *>(&machine_))
                    crt->key_input(value);
                else if (auto *gdp = dynamic_cast<partner_gdp *>(&machine_))
                    ok = gdp->key_input(value);
                if (!ok) break;
                ++accepted;
                for (uint64_t tick = 0; tick < spacing; ++tick) {
                    machine_.tick();
                    ++elapsed;
                    capture_video_if_due();
                }
            }
            return tool_result((name == "press_keys" ? "typed " : "queued ") +
                std::to_string(accepted) + " of " + std::to_string(input.size()) +
                " keyboard bytes", {{"accepted", accepted}, {"requested", input.size()},
                {"ticks", elapsed}, {"tstates", elapsed}, {"cycles", elapsed}});
        }
        if (name == "screen_text") {
            const std::string mode = arguments.value("mode", "chars");
            const bool trim = bool_arg(arguments, "trim", true);
            if (mode == "ascii") {
                const int columns = (int)unsigned_arg(arguments, "columns", 8, 200, 64);
                const int rows = (int)unsigned_arg(arguments, "rows", 4, 100, 24);
                const captured_screen frame = capture_screen();
                static constexpr char ramp[] = " .:-=+*#%@";
                std::string art;
                for (int row = 0; row < rows; ++row) {
                    std::string line;
                    for (int column = 0; column < columns; ++column) {
                        const int x0 = column * frame.width / columns;
                        const int x1 = std::max(x0 + 1,
                            (column + 1) * frame.width / columns);
                        const int y0 = row * frame.height / rows;
                        const int y1 = std::max(y0 + 1,
                            (row + 1) * frame.height / rows);
                        uint64_t sum = 0, count = 0;
                        for (int y = y0; y < y1; ++y) {
                            for (int x = x0; x < x1; ++x) {
                                const size_t offset = ((size_t)y * frame.width + x) * 3;
                                sum += 54 * frame.rgb[offset] +
                                       183 * frame.rgb[offset + 1] +
                                       19 * frame.rgb[offset + 2];
                                ++count;
                            }
                        }
                        const unsigned light = count
                            ? (unsigned)(sum / count / 256) : 0;
                        line.push_back(ramp[light * (sizeof(ramp) - 2) / 255]);
                    }
                    if (trim)
                        while (!line.empty() && line.back() == ' ') line.pop_back();
                    art += line;
                    if (row + 1 < rows) art.push_back('\n');
                }
                return tool_result(art.empty() ? "screen is currently blank" : art,
                    {{"mode", "ascii"}, {"text", art},
                     {"columns", columns}, {"rows", rows}});
            }
            if (mode != "chars")
                throw std::invalid_argument("'mode' must be chars or ascii");
            std::string screen;
            std::string serial;
            if (auto *crt = dynamic_cast<partner_crt *>(&machine_)) {
                screen = crt->dump_terminal_text();
                serial = crt->dump_raw_serial_text();
            } else if (auto *gdp = dynamic_cast<partner_gdp *>(&machine_)) {
                screen = gdp->dump_terminal_text();
                serial = gdp->dump_raw_serial_text();
            }
            if (trim) screen = trim_terminal_text(screen);
            json result = {{"mode", "chars"}, {"screen", screen}, {"text", screen},
                {"raw_serial", serial}, {"printer", machine_.get_virtual_printer_text()},
                {"font_usable", true}};
            return tool_result(screen.empty() ? "screen is currently blank" : screen,
                               std::move(result));
        }
        if (name == "screen" || name == "screenshot") {
            const int scale = (int)unsigned_arg(arguments, "scale", 1, 4, 1);
            const captured_screen frame = capture_screen(scale);
            const auto png = encode_png(frame.rgb, frame.width, frame.height);
            json info = {{"width", frame.width}, {"height", frame.height},
                         {"bytes", png.size()}, {"format", "png"}, {"scale", scale}};
            if (name == "screen")
                return image_tool_result("Partner " + model_ + " screen " +
                    std::to_string(frame.width) + "x" + std::to_string(frame.height),
                    base64_encode(png), std::move(info));
            if (!arguments.contains("path") || !arguments["path"].is_string() ||
                arguments["path"].get<std::string>().empty())
                throw std::invalid_argument("'path' is required");
            const std::string path = arguments["path"].get<std::string>();
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            if (!output) throw std::runtime_error("cannot open screenshot path: " + path);
            output.write(reinterpret_cast<const char *>(png.data()),
                         (std::streamsize)png.size());
            if (!output) throw std::runtime_error("could not write screenshot: " + path);
            info["path"] = path;
            return tool_result("saved PNG screenshot to " + path, std::move(info));
        }
        if (name == "video_start") {
            if (video_file_.is_open())
                throw std::runtime_error("a video recording is already active");
            if (!arguments.contains("path") || !arguments["path"].is_string() ||
                arguments["path"].get<std::string>().empty())
                throw std::invalid_argument("'path' is required");
            video_path_ = arguments["path"].get<std::string>();
            const captured_screen frame = capture_screen();
            video_width_ = frame.width;
            video_height_ = frame.height;
            video_file_.open(video_path_, std::ios::binary | std::ios::trunc);
            if (!video_file_)
                throw std::runtime_error("cannot open video path: " + video_path_);
            video_file_ << "YUV4MPEG2 W" << video_width_ << " H" << video_height_
                        << " F60:1 Ip A0:0 C444\n";
            video_start_tick_ = machine_.get_tick_count();
            video_frames_ = 0;
            return tool_result("started YUV4MPEG2 recording to " + video_path_,
                {{"path", video_path_}, {"width", video_width_},
                 {"height", video_height_}, {"frame_rate", 60},
                 {"start_tick", video_start_tick_}});
        }
        if (name == "video_stop") {
            if (!video_file_.is_open())
                throw std::runtime_error("no video recording is active");
            const std::string path = video_path_;
            const uint64_t frames = video_frames_;
            const uint64_t start_tick = video_start_tick_;
            video_file_.flush();
            video_file_.close();
            std::error_code error;
            const uint64_t bytes = std::filesystem::file_size(path, error);
            json result = {{"path", path}, {"frames", frames},
                {"duration", (double)frames / (double)nominal_frame_rate},
                {"ticks", machine_.get_tick_count() - start_tick},
                {"bytes", error ? 0 : bytes}};
            video_path_.clear();
            return tool_result("stopped recording after " + std::to_string(frames) +
                               " frames", std::move(result));
        }
        if (name == "mount_media") {
            if (!arguments.contains("kind") || !arguments["kind"].is_string() ||
                !arguments.contains("path") || !arguments["path"].is_string())
                throw std::invalid_argument("'kind' and 'path' are required strings");
            const std::string kind = arguments["kind"].get<std::string>();
            const std::string path = arguments["path"].get<std::string>();
            if (kind == "hdd") machine_.load_hdd(path);
            else if (kind.size() == 3 && kind[0] == 'f' && kind[1] == 'd' &&
                     kind[2] >= '0' && kind[2] <= '3')
                machine_.load_disk(kind[2] - '0', path);
            else
                throw std::invalid_argument("'kind' must be fd0, fd1, fd2, fd3, or hdd");
            return tool_result("mounted " + kind + " from " + path,
                               {{"kind", kind}, {"path", path}});
        }
        return tool_result("no tool named '" + name + "'", nullptr, true);
    } catch (const std::exception &error) {
        return tool_result(error.what(), nullptr, true);
    }
}

std::optional<json> idp_mcp_server::handle(const json &message)
{
    if (!message.is_object())
        return rpc_error(nullptr, -32600, "request must be a JSON object");
    const bool notification = !message.contains("id");
    const json id = notification ? json(nullptr) : message["id"];
    if (!message.contains("method") || !message["method"].is_string())
        return notification ? std::nullopt
            : std::optional<json>(rpc_error(id, -32600, "request needs a method"));
    const std::string method = message["method"].get<std::string>();
    if (method == "notifications/initialized") return std::nullopt;
    if (notification) return std::nullopt;
    if (method == "initialize") {
        std::string version = "2025-06-18";
        if (message.contains("params") && message["params"].is_object()) {
            const std::string requested = message["params"].value("protocolVersion", version);
            if (requested == "2025-06-18" || requested == "2025-03-26" ||
                requested == "2024-11-05") version = requested;
        }
        return rpc_response(id, {
            {"protocolVersion", version},
            {"capabilities", {{"tools", {{"listChanged", false}}}}},
            {"serverInfo", {{"name", "idp-mcp"}, {"version", "1.1.0"}}}});
    }
    if (method == "ping") return rpc_response(id, json::object());
    if (method == "tools/list") return rpc_response(id, {{"tools", list_tools()}});
    if (method == "tools/call") {
        if (!message.contains("params") || !message["params"].is_object() ||
            !message["params"].contains("name") ||
            !message["params"]["name"].is_string())
            return rpc_error(id, -32602, "tools/call requires a name string");
        const json call_arguments = message["params"].value("arguments", json::object());
        return rpc_response(id, invoke_tool(
            message["params"]["name"].get<std::string>(), call_arguments));
    }
    return rpc_error(id, -32601, "unsupported method '" + method + "'");
}

std::string idp_mcp_server::handle_line(const std::string &line)
{
    try {
        const auto response = handle(json::parse(line));
        return response ? response->dump() : std::string{};
    } catch (const json::parse_error &) {
        return rpc_error(nullptr, -32700, "parse error").dump();
    }
}
