#pragma once

#include <filesystem>
#include <string_view>

namespace envy::shell_hooks {

inline constexpr int kVersion = 6;

// Parse _ENVY_HOOK_VERSION from the first 5 lines of a hook file.
// Returns 0 if file is missing, unreadable, or has no valid stamp.
int parse_version(std::filesystem::path const &hook_path);

// Parse _ENVY_HOOK_VERSION from raw content (first 5 lines examined).
// Returns 0 if no valid stamp found.
int parse_version_from_content(std::string_view content);

// Write/update all shell hook files in cache_root/shell/.
// Skips hooks that are already at or above kVersion.
// Returns the number of hooks written (0–4).
int ensure(std::filesystem::path const &cache_root);

}  // namespace envy::shell_hooks
