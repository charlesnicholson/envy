#pragma once

#include "lua_util.h"
#include "recipe_spec.h"
#include "shell.h"
#include "util.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

namespace envy {

struct lua_ctx_common;

struct manifest : unmovable {
  std::vector<recipe_spec> packages;
  std::filesystem::path manifest_path;

  manifest() = default;

  // Find manifest path: use provided path if given, otherwise discover from current
  // directory. Returns absolute path or throws if not found
  static std::filesystem::path find_manifest_path(
      std::optional<std::filesystem::path> const &explicit_path);

  static std::optional<std::filesystem::path> discover();

  static std::unique_ptr<manifest> load(std::filesystem::path const &manifest_path);
  static std::unique_ptr<manifest> load(std::vector<unsigned char> const &content,
                                        std::filesystem::path const &manifest_path);
  static std::unique_ptr<manifest> load(char const *script,
                                        std::filesystem::path const &manifest_path);

  // Get default_shell global type and value
  // Returns nullopt if no default_shell specified
  default_shell_cfg_t get_default_shell(lua_ctx_common const *ctx) const;

 private:
  lua_state_ptr lua_state_;  // Lua state (kept alive for default_shell function access)
};

}  // namespace envy
