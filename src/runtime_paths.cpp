#include "runtime_paths.hpp"

#include <SDL.h>

#include <iostream>
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
    return result.lexically_normal();
}

std::vector<fs::path> resource_candidates(const fs::path &relative)
{
    std::vector<fs::path> candidates;
    std::error_code error;
    candidates.push_back(fs::current_path(error) / relative);
    const fs::path executable = executable_directory();
    if (!executable.empty()) {
        candidates.push_back(executable / relative);
        candidates.push_back(executable.parent_path() / relative);
        // Native macOS package layout:
        //   App.app/Contents/MacOS/idp-emu
        //   App.app/Contents/Resources/{roms,disks,assets}
        candidates.push_back(executable.parent_path() / "Resources" / relative);
    }
#ifdef IDP_SOURCE_ROOT
    candidates.push_back(fs::path(IDP_SOURCE_ROOT) / relative);
#endif
    return candidates;
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

std::string mutable_resource_copy(const std::string &relative)
{
    const fs::path source(find_resource(relative));
    const fs::path destination = user_file(relative);
    std::error_code error;
    if (!fs::exists(destination, error)) {
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
    return destination.string();
}
} // namespace runtime_paths
