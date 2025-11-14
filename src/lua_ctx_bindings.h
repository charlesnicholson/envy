#pragma once

#include <filesystem>
#include <string>

struct lua_State;

namespace envy {

struct graph_state;
struct manifest;
struct recipe;

// Common context fields that all phase contexts must provide.
// Phase-specific contexts should embed this as their first member.
struct lua_ctx_common {
  std::filesystem::path fetch_dir;
  std::filesystem::path run_dir;  // ctx.run() (phase-specific: tmp_dir, stage_dir, etc.)
  graph_state *state;             // Still needed for cache access
  recipe *recipe_;                // Current recipe (for ctx.asset() lookups)
  manifest const *manifest_;  // Manifest (for default_shell resolution, always non-null)
};

// Register common Lua context functions available to all phases.
// Each function expects the ctx table to be at the top of the Lua stack.
// The context pointer is bound as an upvalue for the registered function.

// ctx.run(script, opts?) -> {stdout, stderr}
// Execute shell script, log output to TUI, return captured output
void lua_ctx_bindings_register_run(lua_State *lua, void *context);

// ctx.asset(identity) -> path
// Look up dependency in graph_state, verify completed, return asset_path
void lua_ctx_bindings_register_asset(lua_State *lua, void *context);

// ctx.copy(src, dst)
// Copy file or directory (auto-detected)
void lua_ctx_bindings_register_copy(lua_State *lua, void *context);

// ctx.move(src, dst)
// Move/rename file or directory (uses rename when possible)
void lua_ctx_bindings_register_move(lua_State *lua, void *context);

// ctx.extract(filename, opts?)
// Extract single archive with optional strip_components
void lua_ctx_bindings_register_extract(lua_State *lua, void *context);

// ctx.ls(path)
// List directory contents for debugging (prints to TUI)
void lua_ctx_bindings_register_ls(lua_State *lua, void *context);

// Check if target_identity is a declared dependency of current recipe
// Used for ctx.asset() validation. Exposed for testing.
bool is_declared_dependency(recipe *r, std::string const &target_identity);

// Lua C function: ctx.asset(identity) -> path
// Exposed for use in default_shell functions (in manifest.cpp)
int lua_ctx_asset(lua_State *lua);

}  // namespace envy
