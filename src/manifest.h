#pragma once

#include "lua_util.h"
#include "recipe_spec.h"
#include "shell.h"

#include <filesystem>
#include <mutex>
#include <optional>
#include <vector>

namespace envy {

struct lua_ctx_common;

struct manifest {
  std::vector<recipe_spec> packages;
  std::filesystem::path manifest_path;  // Absolute path to envy.lua

  manifest() = default;
  manifest(manifest&&) noexcept;
  manifest& operator=(manifest&&) noexcept;
  manifest(manifest const&) = delete;
  manifest& operator=(manifest const&) = delete;

  static std::optional<std::filesystem::path> discover();
  static manifest load(char const *script, std::filesystem::path const &manifest_path);

  // Resolve default_shell (evaluates function if needed, caches result)
  // Returns nullopt if no default_shell specified
  default_shell_cfg resolve_default_shell(lua_ctx_common const *ctx) const;

private:
  // Parse and store default_shell global
  void parse_default_shell(lua_State *L);
  lua_state_ptr lua_state_;                        // Lua state (kept alive for functions)
  int default_shell_func_ref_{-1};                 // LUA_REGISTRYINDEX ref, -1 if none
  mutable std::once_flag resolve_flag_;
  mutable default_shell_cfg resolved_;             // Cached result (constant or evaluated)
};

}  // namespace envy
