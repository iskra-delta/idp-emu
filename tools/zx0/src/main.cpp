/*
 * Host ZX0 compressor utility.
 */

#include "zx0.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct cli_options {
    zx0::options encode{};
    bool force = false;
    std::filesystem::path input_path{};
    std::filesystem::path output_path{};
    bool output_explicit = false;
};

void print_usage(const char *prog)
{
    std::cerr << "Usage: " << prog << " [options] input [output.zx0]\n";
    std::cerr << "Options:\n";
    std::cerr << "  -f, --force       Overwrite the output file if it already exists\n";
    std::cerr << "  -c, --classic     Write ZX0 classic v1 format\n";
    std::cerr << "  -b, --backwards   Compress for backwards decompression\n";
    std::cerr << "  -q, --quick       Use the fast non-optimal search mode\n";
    std::cerr << "  -s, --skip N      Skip the first N bytes while allowing matches into them\n";
    std::cerr << "  -h, --help        Show this help\n";
}

int parse_positive_int(const std::string &text)
{
    std::size_t used = 0;
    const int value = std::stoi(text, &used, 10);
    if (used != text.size() || value < 0)
        throw std::runtime_error("invalid integer: " + text);
    return value;
}

std::filesystem::path default_output_path(const std::filesystem::path &input_path)
{
    return std::filesystem::path(input_path.string() + ".zx0");
}

std::vector<std::uint8_t> read_file(const std::filesystem::path &path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
        throw std::runtime_error("cannot open input file: " + path.string());

    const std::streamsize size = file.tellg();
    if (size <= 0)
        throw std::runtime_error("input file is empty: " + path.string());

    file.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
    if (!file.read(reinterpret_cast<char *>(data.data()), size))
        throw std::runtime_error("cannot read input file: " + path.string());
    return data;
}

void write_file(const std::filesystem::path &path, const std::vector<std::uint8_t> &data)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file)
        throw std::runtime_error("cannot create output file: " + path.string());
    if (!file.write(reinterpret_cast<const char *>(data.data()),
                    static_cast<std::streamsize>(data.size())))
    {
        throw std::runtime_error("cannot write output file: " + path.string());
    }
}

cli_options parse_args(int argc, char **argv)
{
    cli_options options;

    for (int i = 1; i < argc; i++) {
        const std::string arg = argv[i];

        if ((arg == "-h") || (arg == "--help")) {
            print_usage(argv[0]);
            std::exit(0);
        }
        if ((arg == "-f") || (arg == "--force")) {
            options.force = true;
            continue;
        }
        if ((arg == "-c") || (arg == "--classic")) {
            options.encode.classic = true;
            continue;
        }
        if ((arg == "-b") || (arg == "--backwards")) {
            options.encode.backwards = true;
            continue;
        }
        if ((arg == "-q") || (arg == "--quick")) {
            options.encode.quick = true;
            continue;
        }
        if ((arg == "-s") || (arg == "--skip")) {
            if ((i + 1) >= argc)
                throw std::runtime_error(arg + " requires a value");
            options.encode.skip = parse_positive_int(argv[++i]);
            continue;
        }
        if (!arg.empty() && (arg[0] == '-'))
            throw std::runtime_error("unknown option: " + arg);

        if (options.input_path.empty()) {
            options.input_path = arg;
            continue;
        }
        if (!options.output_explicit) {
            options.output_path = arg;
            options.output_explicit = true;
            continue;
        }
        throw std::runtime_error("too many positional arguments");
    }

    if (options.input_path.empty())
        throw std::runtime_error("missing input file");

    if (!options.output_explicit)
        options.output_path = default_output_path(options.input_path);

    return options;
}

void print_summary(const cli_options &options,
                   std::size_t input_size,
                   std::size_t output_size,
                   int delta)
{
    std::cout << "File";
    if (options.encode.skip != 0)
        std::cout << " partially";
    std::cout << " compressed";
    if (options.encode.backwards)
        std::cout << " backwards";
    std::cout << " from " << (input_size - static_cast<std::size_t>(options.encode.skip))
              << " to " << output_size
              << " bytes! (delta " << delta << ")\n";
}

} // namespace

int main(int argc, char **argv)
{
    try {
        const cli_options options = parse_args(argc, argv);
        const std::vector<std::uint8_t> input = read_file(options.input_path);

        if (!options.force && std::filesystem::exists(options.output_path))
            throw std::runtime_error("output file already exists: " + options.output_path.string());

        const zx0::compression_result result = zx0::compress(input, options.encode, true);
        write_file(options.output_path, result.data);
        print_summary(options, input.size(), result.data.size(), result.delta);
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
