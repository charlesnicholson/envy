#include "lua_ctx_bindings.h"

#include "recipe.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

namespace envy {

// Check if target_identity is a declared dependency of current recipe
// Used for ctx.asset() validation. Exposed for testing.
bool is_declared_dependency(recipe *r, std::string const &target_identity) {
  for (std::string const &dep_identity : r->declared_dependencies) {
    if (dep_identity == target_identity) { return true; }
  }
  return false;
}

// Registration functions

void lua_ctx_bindings_register_run(lua_State *lua, void *context) {
  lua_pushlightuserdata(lua, context);
  lua_pushcclosure(lua, lua_ctx_run, 1);
  lua_setfield(lua, -2, "run");
}

void lua_ctx_bindings_register_asset(lua_State *lua, void *context) {
  lua_pushlightuserdata(lua, context);
  lua_pushcclosure(lua, lua_ctx_asset, 1);
  lua_setfield(lua, -2, "asset");
}

void lua_ctx_bindings_register_copy(lua_State *lua, void *context) {
  lua_pushlightuserdata(lua, context);
  lua_pushcclosure(lua, lua_ctx_copy, 1);
  lua_setfield(lua, -2, "copy");
}

void lua_ctx_bindings_register_move(lua_State *lua, void *context) {
  lua_pushlightuserdata(lua, context);
  lua_pushcclosure(lua, lua_ctx_move, 1);
  lua_setfield(lua, -2, "move");
}

void lua_ctx_bindings_register_extract(lua_State *lua, void *context) {
  lua_pushlightuserdata(lua, context);
  lua_pushcclosure(lua, lua_ctx_extract, 1);
  lua_setfield(lua, -2, "extract");
}

void lua_ctx_bindings_register_ls(lua_State *lua, void *context) {
  lua_pushlightuserdata(lua, context);
  lua_pushcclosure(lua, lua_ctx_ls, 1);
  lua_setfield(lua, -2, "ls");
}

void lua_ctx_bindings_register_fetch_phase(lua_State *lua, fetch_phase_ctx *context) {
  // ctx.fetch
  lua_pushlightuserdata(lua, context);
  lua_pushcclosure(lua, lua_ctx_fetch, 1);
  lua_setfield(lua, -2, "fetch");

  // ctx.commit_fetch
  lua_pushlightuserdata(lua, context);
  lua_pushcclosure(lua, lua_ctx_commit_fetch, 1);
  lua_setfield(lua, -2, "commit_fetch");
}

}  // namespace envy
