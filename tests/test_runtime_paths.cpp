#include "runtime_paths.hpp"

#include <SDL.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

int fail(const char *message)
{
    std::fprintf(stderr, "FAIL %s\n", message);
    return 1;
}

} // namespace

int main()
{
    namespace fs = std::filesystem;

    char *base = SDL_GetBasePath();
    if (!base)
        return fail("SDL_GetBasePath returned no executable directory");
    fs::path executable_directory(base);
    SDL_free(base);
    executable_directory = executable_directory.lexically_normal();
    if (executable_directory.filename().empty())
        executable_directory = executable_directory.parent_path();

    const fs::path sentinel =
        executable_directory.parent_path() / "runtime-paths-sentinel.dat";
    {
        std::ofstream output(sentinel, std::ios::binary | std::ios::trunc);
        output << "bundle root resource\n";
        if (!output)
            return fail("could not create bundle-root sentinel");
    }

    const fs::path resolved =
        fs::path(runtime_paths::find_resource(sentinel.filename().string()))
            .lexically_normal();
    std::error_code error;
    fs::remove(sentinel, error);

    if (resolved != sentinel.lexically_normal())
        return fail("bundle-root resource above the executable was not found");

    std::puts("PASS runtime_paths");
    return 0;
}
