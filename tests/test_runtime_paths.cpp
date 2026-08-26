#include "runtime_paths.hpp"

#include <SDL.h>

#include <chrono>
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

bool write_text(const std::filesystem::path &path, const char *text)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << text;
    return static_cast<bool>(output);
}

std::string read_text(const std::filesystem::path &path)
{
    std::ifstream input(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input),
                       std::istreambuf_iterator<char>());
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

    const auto unique = std::chrono::steady_clock::now()
                            .time_since_epoch().count();
    const fs::path scratch = fs::path(IDP_SOURCE_ROOT) / "tests/dump" /
        ("runtime-paths-" + std::to_string(unique));
    const fs::path conflicting_cwd = scratch / "cwd";
    fs::create_directories(conflicting_cwd);

    const fs::path sentinel =
        executable_directory.parent_path() / "runtime-paths-sentinel.dat";
    {
        std::ofstream output(sentinel, std::ios::binary | std::ios::trunc);
        output << "bundle root resource\n";
        if (!output)
            return fail("could not create bundle-root sentinel");
    }
    if (!write_text(conflicting_cwd / sentinel.filename(),
                    "wrong cwd resource\n"))
        return fail("could not create conflicting cwd resource");

    const fs::path original_cwd = fs::current_path();
    fs::current_path(conflicting_cwd);
    const fs::path resolved =
        fs::path(runtime_paths::find_resource(sentinel.filename().string()))
            .lexically_normal();
    fs::current_path(original_cwd);
    std::error_code error;
    fs::remove(sentinel, error);

    if (resolved != sentinel.lexically_normal())
        return fail("cwd overrode the packaged bundle resource");

    const fs::path source = scratch / "package/system.img";
    const fs::path destination = scratch / "user/system.img";
    fs::create_directories(source.parent_path());
    if (!write_text(source, "packaged seed one"))
        return fail("could not create packaged seed");

    if (runtime_paths::synchronize_mutable_copy(source, destination) !=
        destination.string())
        return fail("initial mutable copy returned the wrong path");
    if (read_text(destination) != "packaged seed one")
        return fail("initial packaged seed was not copied");

    if (!write_text(destination, "user data on seed one"))
        return fail("could not modify writable media");
    (void)runtime_paths::synchronize_mutable_copy(source, destination);
    if (read_text(destination) != "user data on seed one")
        return fail("user media changed while packaged seed was unchanged");

    if (!write_text(source, "packaged seed two"))
        return fail("could not update packaged seed");
    (void)runtime_paths::synchronize_mutable_copy(source, destination);
    if (read_text(destination) != "packaged seed two")
        return fail("updated packaged seed was not activated");
    fs::path backup = destination;
    backup += ".previous";
    if (read_text(backup) != "user data on seed one")
        return fail("previous writable media was not preserved");

    if (!write_text(destination, "user data on seed two"))
        return fail("could not modify refreshed writable media");
    (void)runtime_paths::synchronize_mutable_copy(source, destination);
    if (read_text(destination) != "user data on seed two")
        return fail("refreshed user media was not retained");

    fs::remove_all(scratch, error);

    std::puts("PASS runtime_paths");
    return 0;
}
