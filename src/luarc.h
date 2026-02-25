#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace envy {

// Convert absolute path to use $HOME for portability in .luarc.json
std::string make_portable_path(std::filesystem::path const &path);

// Pure transform: given .luarc.json content and expected types path,
// returns updated JSON if the envy entry was changed, or nullopt if no change.
std::optional<std::string> rewrite_luarc_types_path(std::string_view content,
                                                    std::string_view expected_path);

// Update envy types path in existing .luarc.json when version changes.
// No-op if file missing, no workspace.library, or no envy entry.
void update_luarc_types_path(std::filesystem::path const &project_dir,
                             std::filesystem::path const &cache_root);

std::filesystem::path extract_lua_ls_types();

void write_luarc(std::filesystem::path const &project_dir,
                 std::filesystem::path const &types_dir);

}  // namespace envy
