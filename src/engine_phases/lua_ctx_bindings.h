#pragma once

struct lua_State;

namespace envy {

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

}  // namespace envy
