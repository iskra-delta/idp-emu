#include "runtime_paths.hpp"

#include <SDL.h>

#include <array>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <system_error>
#include <vector>

namespace runtime_paths {
namespace {
namespace fs = std::filesystem;

fs::path executable_directory()
{
    char *base = SDL_GetBasePath();
    if (!base)
        return {};
    fs::path result(base);
    SDL_free(base);
    result = result.lexically_normal();
    // SDL returns a directory with a trailing separator. std::filesystem
    // represents that as an empty filename, so parent_path() would otherwise
    // return the same directory instead of the bundle root one level above.
    if (result.filename().empty())
        result = result.parent_path();
    return result;
}

std::vector<fs::path> resource_candidates(const fs::path &relative)
{
    std::vector<fs::path> candidates;
    std::error_code error;
    const fs::path executable = executable_directory();
    if (!executable.empty()) {
        candidates.push_back(executable / relative);
        candidates.push_back(executable.parent_path() / relative);
        // Native macOS package layout:
        //   App.app/Contents/MacOS/idp-emu
        //   App.app/Contents/Resources/{roms,disks,assets}
        candidates.push_back(executable.parent_path() / "Resources" / relative);
    }
    // Developer builds normally find resources through cwd. Installed
    // executables must prefer their own package to avoid mixing versions.
    candidates.push_back(fs::current_path(error) / relative);
#ifdef IDP_SOURCE_ROOT
    candidates.push_back(fs::path(IDP_SOURCE_ROOT) / relative);
#endif
    return candidates;
}

std::optional<std::string> file_fingerprint(const fs::path &path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return std::nullopt;

    constexpr std::uint64_t fnv_offset = 14695981039346656037ULL;
    constexpr std::uint64_t fnv_prime = 1099511628211ULL;
    std::uint64_t hash = fnv_offset;
    std::uint64_t size = 0;
    std::array<char, 64 * 1024> buffer{};
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize count = input.gcount();
        for (std::streamsize i = 0; i < count; ++i) {
            hash ^= static_cast<unsigned char>(
                buffer[static_cast<std::size_t>(i)]);
            hash *= fnv_prime;
        }
        size += static_cast<std::uint64_t>(count);
    }
    if (!input.eof())
        return std::nullopt;

    std::ostringstream result;
    result << "idp-seed-v1 " << size << ' '
           << std::hex << std::setfill('0') << std::setw(16) << hash;
    return result.str();
}

fs::path seed_marker_path(const fs::path &destination)
{
    fs::path marker = destination;
    marker += ".idp-seed";
    return marker;
}

std::optional<std::string> read_seed_marker(const fs::path &marker)
{
    std::ifstream input(marker);
    std::string value;
    if (!std::getline(input, value) || value.empty())
        return std::nullopt;
    return value;
}

bool write_seed_marker(const fs::path &marker, const std::string &fingerprint)
{
    std::ofstream output(marker, std::ios::trunc);
    output << fingerprint << '\n';
    return static_cast<bool>(output);
}

fs::path available_backup_path(const fs::path &destination)
{
    std::error_code error;
    fs::path candidate = destination;
    candidate += ".previous";
    for (unsigned int suffix = 1; fs::exists(candidate, error); ++suffix) {
        error.clear();
        candidate = destination;
        candidate += ".previous." + std::to_string(suffix);
    }
    return candidate;
}
} // namespace

std::string find_resource(const std::string &relative)
{
    if (relative.empty())
        return relative;
    const fs::path input(relative);
    if (input.is_absolute())
        return input.lexically_normal().string();
    std::error_code error;
    for (const fs::path &candidate : resource_candidates(input)) {
        error.clear();
        if (fs::exists(candidate, error) && !error)
            return candidate.lexically_normal().string();
    }
    return relative;
}

fs::path user_file(const fs::path &relative)
{
    fs::path root;
    if (char *preference = SDL_GetPrefPath("Iskra Delta", "Partner Emulator")) {
        root = preference;
        SDL_free(preference);
    } else {
        std::error_code error;
        root = fs::current_path(error) / "idp-emu-data";
    }
    const fs::path result = (root / relative).lexically_normal();
    std::error_code error;
    fs::create_directories(result.parent_path(), error);
    return result;
}

std::string synchronize_mutable_copy(
    const fs::path &source, const fs::path &destination)
{
    const auto source_fingerprint = file_fingerprint(source);
    if (!source_fingerprint) {
        std::cerr << "[warning] Could not read packaged media seed '"
                  << source.string() << "'\n";
        return source.string();
    }

    std::error_code error;
    fs::create_directories(destination.parent_path(), error);
    if (error) {
        std::cerr << "[warning] Could not create writable media directory '"
                  << destination.parent_path().string() << "': "
                  << error.message() << "\n";
        return source.string();
    }

    const fs::path marker = seed_marker_path(destination);
    const bool destination_exists = fs::exists(destination, error) && !error;
    if (destination_exists) {
        const auto installed_seed = read_seed_marker(marker);
        if (installed_seed && *installed_seed == *source_fingerprint)
            return destination.string();

        // An identical legacy copy needs only a marker, not a backup.
        const auto destination_fingerprint = file_fingerprint(destination);
        if (destination_fingerprint &&
            *destination_fingerprint == *source_fingerprint) {
            if (!write_seed_marker(marker, *source_fingerprint))
                std::cerr << "[warning] Could not record media seed marker '"
                          << marker.string() << "'\n";
            return destination.string();
        }

        const fs::path backup = available_backup_path(destination);
        error.clear();
        fs::rename(destination, backup, error);
        if (error) {
            std::cerr << "[warning] Could not preserve obsolete writable media '"
                      << destination.string() << "': " << error.message() << "\n";
            return destination.string();
        }

        error.clear();
        fs::copy_file(source, destination, fs::copy_options::none, error);
        if (error) {
            std::error_code restore_error;
            fs::rename(backup, destination, restore_error);
            std::cerr << "[warning] Could not activate packaged media seed '"
                      << source.string() << "': " << error.message() << "\n";
            return restore_error ? source.string() : destination.string();
        }

        std::cerr << "[info] Updated writable media from packaged seed: "
                  << destination.string() << "\n"
                  << "[info] Previous writable media preserved as: "
                  << backup.string() << "\n";
    } else {
        error.clear();
        fs::copy_file(source, destination, fs::copy_options::none, error);
        if (error) {
            std::cerr << "[warning] Could not create writable media copy '"
                      << destination.string() << "': " << error.message() << "\n";
            return source.string();
        }
        std::cerr << "[info] Created writable media copy: "
                  << destination.string() << "\n";
    }

    if (!write_seed_marker(marker, *source_fingerprint))
        std::cerr << "[warning] Could not record media seed marker '"
                  << marker.string() << "'\n";
    return destination.string();
}

std::string mutable_resource_copy(const std::string &relative)
{
    const fs::path source(find_resource(relative));
    const fs::path destination = user_file(relative);
    return synchronize_mutable_copy(source, destination);
}
} // namespace runtime_paths
