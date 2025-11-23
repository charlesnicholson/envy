#pragma once

#include <filesystem>
#include <string>
#include <unordered_set>

struct lua_State;

namespace envy {

class engine;
struct manifest;
struct recipe;

// Common context fields that all phase contexts must provide.
// Phase-specific contexts should embed this as their first member.
struct lua_ctx_common {
  std::filesystem::path fetch_dir;
  std::filesystem::path run_dir;  // ctx.run() (phase-specific: tmp_dir, stage_dir, etc.)
  engine *engine_;                // Engine for cache access
  recipe *recipe_;                // Current recipe (for ctx.asset() lookups)
};

// Fetch-phase-specific context (extends lua_ctx_common).
// Used by both recipe_fetch and asset_fetch phases.
struct fetch_phase_ctx : lua_ctx_common {
  std::filesystem::path stage_dir;  // Git repos bypass tmp, go directly here
  std::unordered_set<std::string> used_basenames;  // Collision detection across ctx.fetch() calls
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

// Register fetch-phase bindings (ctx.fetch + ctx.commit_fetch)
// Requires fetch_phase_ctx* as context (extends lua_ctx_common)
// Used by both recipe_fetch and asset_fetch phases
void lua_ctx_bindings_register_fetch_phase(lua_State *lua, fetch_phase_ctx *context);

// Check if target_identity is a declared dependency of current recipe
// Used for ctx.asset() validation. Exposed for testing.
bool is_declared_dependency(recipe *r, std::string const &target_identity);

// Lua C function implementations (one per file)
int lua_ctx_run(lua_State *lua);
int lua_ctx_asset(lua_State *lua);
int lua_ctx_copy(lua_State *lua);
int lua_ctx_move(lua_State *lua);
int lua_ctx_extract(lua_State *lua);
int lua_ctx_ls(lua_State *lua);
int lua_ctx_fetch(lua_State *lua);
int lua_ctx_commit_fetch(lua_State *lua);

}  // namespace envy
