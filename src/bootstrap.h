#pragma once

#include "util.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace envy {

inline constexpr std::string_view kEnvyDownloadUrl{
  "https://github.com/charlesnicholson/envy/releases/download"
};

// Check if file contains "envy-managed" marker
bool bootstrap_is_envy_managed(std::filesystem::path const &path);

// Write bootstrap script to bin_dir for the given platform.
// - If file exists without marker: throws
// - If file exists with marker: updates if content differs
// - If file absent: creates
// Returns true if file was written, false if unchanged.
bool bootstrap_write_script(std::filesystem::path const &bin_dir,
                            std::optional<std::string> const &mirror,
                            platform_id platform);

}  // namespace envy
