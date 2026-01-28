#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace envy {

// Check if file contains "envy-managed" marker
bool bootstrap_is_envy_managed(std::filesystem::path const &path);

// Write bootstrap script to bin_dir.
// - If file exists without marker: throws
// - If file exists with marker: updates if content differs
// - If file absent: creates
// Returns true if file was written, false if unchanged.
bool bootstrap_write_script(std::filesystem::path const &bin_dir,
                            std::optional<std::string> const &mirror);

}  // namespace envy
