#pragma once

#include "manifest.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace envy {

// Convert absolute path to use $HOME for portability in .luarc.json
std::string make_portable_path(std::filesystem::path const &path);

// Compute canonical workspace.library entries covering all platforms.
std::vector<std::string> compute_canonical_luarc_paths(envy_meta const &meta);

// Pure transform: given .luarc.json content and canonical paths,
// returns updated JSON if the envy entries changed, or nullopt if no change.
std::optional<std::string> rewrite_luarc_types_path(
    std::string_view content,
    std::vector<std::string> const &canonical_paths);

// Update envy types paths in existing .luarc.json when version changes.
// No-op if file missing, no workspace.library, or already correct.
void update_luarc_types_path(std::filesystem::path const &project_dir,
                             envy_meta const &meta);

std::filesystem::path extract_lua_ls_types(std::filesystem::path const &cache_root);

void write_luarc(std::filesystem::path const &project_dir, envy_meta const &meta);

}  // namespace envy
