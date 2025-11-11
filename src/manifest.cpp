#include "manifest.h"

#include "engine_phases/graph_state.h"
#include "engine_phases/lua_ctx_bindings.h"
#include "lua_util.h"
#include "shell.h"
#include "tui.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

#include <stdexcept>

namespace envy {
namespace {

// Wrapper to add "default_shell:" prefix to error messages
custom_shell parse_custom_shell_table(lua_State *L) {
  try {
    return shell_parse_custom_from_lua(L);
  } catch (std::exception const &e) {
    throw std::runtime_error(std::string{ "default_shell: " } + e.what());
  }
}

// Lua C function for ctx.asset() in default_shell function context
// (Minimal version - no dependency validation, just lookup)
int lua_ctx_asset_for_manifest(lua_State *lua) {
  auto *ctx{ static_cast<lua_ctx_common *>(lua_touserdata(lua, lua_upvalueindex(1))) };
  if (!ctx) { return luaL_error(lua, "ctx.asset: missing context"); }

  // Arg 1: identity (required)
  if (!lua_isstring(lua, 1)) {
    return luaL_error(lua, "ctx.asset: first argument must be identity string");
  }
  char const *identity{ lua_tostring(lua, 1) };

  // Look up dependency in graph_state
  typename decltype(ctx->state->recipes)::const_accessor acc;
  if (!ctx->state->recipes.find(acc, identity)) {
    return luaL_error(lua, "ctx.asset: dependency not found: %s", identity);
  }

  // Verify dependency is completed
  if (!acc->second.completed.load()) {
    return luaL_error(lua, "ctx.asset: dependency not completed: %s", identity);
  }

  // Return asset_path
  std::string const path{ acc->second.asset_path.string() };
  lua_pushstring(lua, path.c_str());
  return 1;
}

}  // namespace

void manifest::parse_default_shell(lua_State *L) {
  lua_getglobal(L, "default_shell");
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1);
    resolved_ = std::nullopt;
    default_shell_func_ref_ = -1;
    return;
  }

  int const value_type{ lua_type(L, -1) };

  // Check for ENVY_SHELL constant (light userdata)
  if (value_type == LUA_TLIGHTUSERDATA) {
    void *ud{ lua_touserdata(L, -1) };
    auto const choice{ static_cast<shell_choice>(reinterpret_cast<uintptr_t>(ud)) };
    lua_pop(L, 1);

    // Validate enum value
    if (choice != shell_choice::bash && choice != shell_choice::sh &&
        choice != shell_choice::cmd && choice != shell_choice::powershell) {
      throw std::runtime_error(
          "default_shell: invalid ENVY_SHELL constant (corrupted light userdata)");
    }

    resolved_ = choice;
    default_shell_func_ref_ = -1;
    return;
  }

  // Check for custom shell table
  if (value_type == LUA_TTABLE) {
    custom_shell result{ parse_custom_shell_table(L) };
    lua_pop(L, 1);

    // Validate custom shell immediately
    shell_validate_custom(result);

    resolved_ = std::move(result);
    default_shell_func_ref_ = -1;
    return;
  }

  // Check for function - store reference for lazy evaluation
  if (value_type == LUA_TFUNCTION) {
    default_shell_func_ref_ = luaL_ref(L, LUA_REGISTRYINDEX);  // Pops function
    resolved_ = std::nullopt;  // Will be evaluated on first resolve_default_shell()
    return;
  }

  // Unsupported type
  lua_pop(L, 1);
  throw std::runtime_error(
      "default_shell: must be ENVY_SHELL constant, table {file=..., ext=...} or "
      "{inline=...}, "
      "or function()");
}

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
  m->lua_state_ = std::move(state);  // Keep lua_state alive for function evaluation

  auto packages{ lua_global_to_array(m->lua_state_.get(), "packages") };
  if (!packages) { throw std::runtime_error("Manifest must define 'packages' global"); }

  for (auto const &package : *packages) {
    m->packages.push_back(recipe_spec::parse(package, manifest_path));
  }

  // Parse and store default_shell configuration
  m->parse_default_shell(m->lua_state_.get());

  return m;
}

default_shell_cfg manifest::resolve_default_shell(lua_ctx_common const *ctx) const {
  // Evaluate function on first call (if needed)
  std::call_once(resolve_flag_, [this, ctx]() {
    if (default_shell_func_ref_ == -1) {
      return;  // Already resolved (constant or table)
    }

    lua_State *L{ lua_state_.get() };
    if (!L) {
      throw std::runtime_error("default_shell function: lua_state not available");
    }

    // Push function from registry
    lua_rawgeti(L, LUA_REGISTRYINDEX, default_shell_func_ref_);
    if (!lua_isfunction(L, -1)) {
      lua_pop(L, 1);
      throw std::runtime_error("default_shell: stored reference is not a function");
    }

    // Build ctx table with ctx.asset() binding (if ctx available)
    int num_args{ 0 };
    if (ctx && ctx->state) {
      lua_createtable(L, 0, 1);  // Create ctx table

      // Register ctx.asset() binding
      lua_pushlightuserdata(L, const_cast<lua_ctx_common *>(ctx));
      lua_pushcclosure(L, lua_ctx_asset_for_manifest, 1);
      lua_setfield(L, -2, "asset");

      num_args = 1;
    }

    // Call function
    if (lua_pcall(L, num_args, 1, 0) != LUA_OK) {
      char const *err{ lua_tostring(L, -1) };
      std::string error_msg{ err ? err : "unknown error" };
      lua_pop(L, 1);
      throw std::runtime_error("default_shell function failed: " + error_msg);
    }

    // Parse returned value (should be ENVY_SHELL constant or table)
    int const return_type{ lua_type(L, -1) };

    if (return_type == LUA_TLIGHTUSERDATA) {
      void *ud{ lua_touserdata(L, -1) };
      auto const choice{ static_cast<shell_choice>(reinterpret_cast<uintptr_t>(ud)) };
      lua_pop(L, 1);

      if (choice != shell_choice::bash && choice != shell_choice::sh &&
          choice != shell_choice::cmd && choice != shell_choice::powershell) {
        throw std::runtime_error(
            "default_shell function: returned invalid ENVY_SHELL constant");
      }

      resolved_ = choice;
    } else if (return_type == LUA_TTABLE) {
      custom_shell result{ parse_custom_shell_table(L) };
      lua_pop(L, 1);

      // Validate custom shell
      shell_validate_custom(result);

      resolved_ = std::move(result);
    } else {
      lua_pop(L, 1);
      throw std::runtime_error(
          "default_shell function: must return ENVY_SHELL constant or table {file=..., "
          "ext=...} "
          "or {inline=...}");
    }
  });

  return resolved_;
}

}  // namespace envy
