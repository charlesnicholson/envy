#pragma once

#include "util.h"

#include <filesystem>

namespace envy {

// Check if file contains "envy-managed" marker
bool bootstrap_is_envy_managed(std::filesystem::path const &path);

// Write bootstrap script to bin_dir for the given platform.
// - If file exists without marker: throws
// - If file exists with marker: updates if content differs
// - If file absent: creates
// Returns true if file was written, false if unchanged.
//
// Carries no project configuration: the mirror, the pinned version, and the sums pin are
// all read out of the manifest at run time by the script itself. Only envy's own upstream
// URLs are stamped, so a script is identical across every project on a given envy version.
bool bootstrap_write_script(std::filesystem::path const &bin_dir, platform_id platform);

}  // namespace envy
