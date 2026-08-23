#pragma once

#include <filesystem>
#include <string>

namespace runtime_paths {

// Find a read-only resource in a source checkout, portable runtime tree, or
// macOS application bundle. Returns the original relative name if not found.
std::string find_resource(const std::string &relative);

// Return a per-user writable location, creating its parent directories.
std::filesystem::path user_file(const std::filesystem::path &relative);

// Synchronize a packaged seed with a writable destination. User changes are
// retained while the packaged seed is unchanged. When the seed changes, the
// previous writable image is backed up before the new seed is activated.
std::string synchronize_mutable_copy(
    const std::filesystem::path &source,
    const std::filesystem::path &destination);

// Copy an installed seed disk to the user profile and keep it synchronized
// across package upgrades. This prevents guest writes from targeting /opt,
// Program Files, or a .app.
std::string mutable_resource_copy(const std::string &relative);

} // namespace runtime_paths
