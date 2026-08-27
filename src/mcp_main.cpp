#include "mcp/idp_mcp_server.hpp"
#include "partner_crt.hpp"
#include "partner_gdp.hpp"
#include "runtime_paths.hpp"

#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef IDP_VERSION
#define IDP_VERSION "dev"
#endif

namespace {
struct options {
    std::string model = "crt";
    std::string rom;
    std::string nvram;
    std::string hdd;
    std::string terminal;
    std::vector<std::pair<int, std::string>> floppies;
    bool no_rom = false;
    bool verbose = false;
    bool list_tools = false;
    bool help = false;
    bool version = false;
};

void usage(std::ostream &out)
{
    out << "usage: idp-mcp [options]\n\n"
        << "Invisible Iskra Delta Partner emulator and MCP stdio server.\n"
        << "stdout carries JSON-RPC only while serving.\n\n"
        << "  --model crt|gdp   machine model (default: crt)\n"
        << "  --rom FILE        boot ROM (default: model ROM in roms/)\n"
        << "  --no-rom          start without a ROM\n"
        << "  --fd0 FILE        attach floppy drive 0 (also --fd1..--fd3)\n"
        << "  --hdd FILE        attach SASI hard-disk image\n"
        << "  --nvram FILE      persist the eight-byte RTC shadow; default is ephemeral\n"
        << "  --terminal TYPE   vt52, vt100, or ansi (default: model profile)\n"
        << "  --list-tools      print MCP tool definitions and exit\n"
        << "  --verbose         log protocol messages to stderr\n"
        << "  --version         print version and exit\n"
        << "  --help            print this help and exit\n";
}

std::string take_value(int &i, int argc, char **argv, const char *option)
{
    if (++i >= argc)
        throw std::invalid_argument(std::string(option) + " needs a value");
    return argv[i];
}

options parse_options(int argc, char **argv)
{
    options out;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") out.help = true;
        else if (arg == "--version") out.version = true;
        else if (arg == "--verbose" || arg == "-v") out.verbose = true;
        else if (arg == "--list-tools") out.list_tools = true;
        else if (arg == "--no-rom") out.no_rom = true;
        else if (arg == "--model") out.model = take_value(i, argc, argv, "--model");
        else if (arg == "--rom") out.rom = take_value(i, argc, argv, "--rom");
        else if (arg == "--hdd") out.hdd = take_value(i, argc, argv, "--hdd");
        else if (arg == "--nvram") out.nvram = take_value(i, argc, argv, "--nvram");
        else if (arg == "--terminal") out.terminal = take_value(i, argc, argv, "--terminal");
        else if (arg.size() == 5 && arg.rfind("--fd", 0) == 0 &&
                 arg[4] >= '0' && arg[4] <= '3') {
            out.floppies.emplace_back(arg[4] - '0',
                                      take_value(i, argc, argv, arg.c_str()));
        } else {
            throw std::invalid_argument("unknown option '" + arg + "'");
        }
    }
    if (out.model != "crt" && out.model != "gdp")
        throw std::invalid_argument("--model must be crt or gdp");
    if (!out.terminal.empty() && out.terminal != "vt52" &&
        out.terminal != "vt100" && out.terminal != "ansi")
        throw std::invalid_argument("--terminal must be vt52, vt100, or ansi");
    if (out.terminal.empty())
        out.terminal = out.model == "gdp" ? "vt100" : "vt52";
    return out;
}

terminal_profile profile_for(const std::string &name)
{
    if (name == "vt52") return terminal_profile::vt52;
    return terminal_profile::vt100_ansi;
}
} // namespace

int main(int argc, char **argv)
{
    try {
        const options settings = parse_options(argc, argv);
        if (settings.help) {
            usage(std::cout);
            return 0;
        }
        if (settings.version) {
            std::cout << "idp-mcp " IDP_VERSION "\n";
            return 0;
        }

        std::unique_ptr<partner> machine;
        if (settings.model == "gdp")
            machine = std::make_unique<partner_gdp>(profile_for(settings.terminal),
                                                     settings.nvram);
        else
            machine = std::make_unique<partner_crt>(profile_for(settings.terminal),
                                                     settings.nvram);

        if (!settings.no_rom) {
            const std::string rom = runtime_paths::find_resource(
                settings.rom.empty()
                    ? "roms/partner_" + settings.model + ".rom"
                    : settings.rom);
            machine->load_rom(rom);
        }
        for (const auto &[drive, path] : settings.floppies)
            machine->load_disk(drive, runtime_paths::find_resource(path));
        if (!settings.hdd.empty())
            machine->load_hdd(runtime_paths::find_resource(settings.hdd));
        machine->reset();

        idp_mcp_server server(*machine, settings.model);
        if (settings.list_tools) {
            std::cout << server.list_tools().dump(2) << '\n';
            return 0;
        }
        if (settings.verbose)
            std::cerr << "idp-mcp: serving " << server.list_tools().size()
                      << " tools on stdio\n";

        std::ios::sync_with_stdio(false);
        std::string line;
        while (std::getline(std::cin, line)) {
            if (line.empty())
                continue;
            if (settings.verbose)
                std::cerr << "idp-mcp <= " << line << '\n';
            const std::string response = server.handle_line(line);
            if (!response.empty()) {
                if (settings.verbose)
                    std::cerr << "idp-mcp => " << response << '\n';
                std::cout << response << '\n' << std::flush;
            }
        }
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "idp-mcp: " << error.what() << '\n';
        return 2;
    }
}
