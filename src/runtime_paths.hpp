#pragma once

#include <filesystem>
#include <string>

namespace runtime_paths {

// Find a read-only resource in a source checkout, portable runtime tree, or
// macOS application bundle. Returns the original relative name if not found.
std::string find_resource(const std::string &relative);

// Return a per-user writable location, creating its parent directories.
std::filesystem::path user_file(const std::filesystem::path &relative);

// Copy an installed seed disk to the user profile once, then return the copy.
// This prevents guest writes from targeting /opt, Program Files, or a .app.
std::string mutable_resource_copy(const std::string &relative);

} // namespace runtime_paths
