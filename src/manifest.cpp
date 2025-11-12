#include "manifest.h"

#include "engine_phases/graph_state.h"
#include "engine_phases/lua_ctx_bindings.h"
#include "lua_shell.h"
#include "lua_util.h"
#include "shell.h"
#include "tui.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

#include <stdexcept>

namespace envy {

std::optional<std::filesystem::path> manifest::discover() {
  namespace fs = std::filesystem;

  auto cur{ fs::current_path() };

  for (;;) {
    auto const manifest_path{ cur / "envy.lua" };
    if (fs::exists(manifest_path)) { return manifest_path; }

    auto const git_path{ cur / ".git" };
    if (fs::exists(git_path) && fs::is_directory(git_path)) { return std::nullopt; }

    auto const parent{ cur.parent_path() };
    if (parent == cur) { return std::nullopt; }

    cur = parent;
  }
}

std::unique_ptr<manifest> manifest::load(char const *script,
                                         std::filesystem::path const &manifest_path) {
  auto state{ lua_make() };
  if (!state) { throw std::runtime_error("Failed to create Lua state"); }

  lua_add_envy(state);

  if (!lua_run_string(state, script)) {
    throw std::runtime_error("Failed to execute manifest script");
  }

  auto m{ std::make_unique<manifest>() };
  m->manifest_path = manifest_path;
  m->lua_state_ = std::move(state);  // Keep lua_state alive for default_shell access

  auto packages{ lua_global_to_array(m->lua_state_.get(), "packages") };
  if (!packages) { throw std::runtime_error("Manifest must define 'packages' global"); }

  for (auto const &package : *packages) {
    m->packages.push_back(recipe_spec::parse(package, manifest_path));
  }

  return m;
}

default_shell_cfg_t manifest::get_default_shell(lua_ctx_common const *ctx) const {
  lua_State *L{ lua_state_.get() };
  if (!L) { return std::nullopt; }

  // Get default_shell global (evaluated fresh every time)
  lua_getglobal(L, "default_shell");
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1);
    return std::nullopt;
  }

  int const value_type{ lua_type(L, -1) };

  // Function - evaluate dynamically with current ctx
  if (value_type == LUA_TFUNCTION) {
    // Build ctx table with ctx.asset() binding
    lua_createtable(L, 0, 1);  // Create ctx table

    // Register ctx.asset() - use the regular validated version
    lua_pushlightuserdata(L, const_cast<lua_ctx_common *>(ctx));
    lua_pushcclosure(L, lua_ctx_asset, 1);
    lua_setfield(L, -2, "asset");

    // Call function(ctx)
    if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
      char const *err{ lua_tostring(L, -1) };
      std::string error_msg{ err ? err : "unknown error" };
      lua_pop(L, 1);
      throw std::runtime_error("default_shell function failed: " + error_msg);
    }

    // Parse returned value using unified parser
    try {
      auto parsed{ parse_shell_config_from_lua(L, -1, "default_shell function") };
      lua_pop(L, 1);

      // Convert flat variant to nested variant structure
      default_shell_value result;
      if (std::holds_alternative<shell_choice>(parsed)) {
        result = std::get<shell_choice>(parsed);
      } else if (std::holds_alternative<custom_shell_file>(parsed)) {
        result = custom_shell{ std::get<custom_shell_file>(parsed) };
      } else {
        result = custom_shell{ std::get<custom_shell_inline>(parsed) };
      }
      return result;
    } catch (std::exception const &e) {
      lua_pop(L, 1);
      throw;
    }
  }

  // Parse constant or table using unified parser
  try {
    auto parsed{ parse_shell_config_from_lua(L, -1, "default_shell") };
    lua_pop(L, 1);

    // Convert flat variant to nested variant structure
    default_shell_value result;
    if (std::holds_alternative<shell_choice>(parsed)) {
      result = std::get<shell_choice>(parsed);
    } else if (std::holds_alternative<custom_shell_file>(parsed)) {
      result = custom_shell{ std::get<custom_shell_file>(parsed) };
    } else {
      result = custom_shell{ std::get<custom_shell_inline>(parsed) };
    }
    return result;
  } catch (std::exception const &e) {
    lua_pop(L, 1);
    throw;
  }
}

}  // namespace envy
