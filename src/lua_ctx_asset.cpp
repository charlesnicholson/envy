#include "lua_ctx_bindings.h"

#include "recipe.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

namespace envy {

// Lua C function: ctx.asset(identity) -> path
// Graph topology guarantees dependency completion before parent accesses it
int lua_ctx_asset(lua_State *lua) {
  auto const *ctx{ static_cast<lua_ctx_common *>(
      lua_touserdata(lua, lua_upvalueindex(1))) };
  if (!ctx || !ctx->recipe_) { return luaL_error(lua, "ctx.asset: missing context"); }

  // Arg 1: identity (required)
  if (!lua_isstring(lua, 1)) {
    return luaL_error(lua, "ctx.asset: first argument must be identity string");
  }
  char const *identity{ lua_tostring(lua, 1) };

  // Validate dependency declaration (only checks direct dependencies)
  if (!is_declared_dependency(ctx->recipe_, identity)) {
    return luaL_error(lua,
                      "ctx.asset: recipe '%s' does not declare dependency on '%s'",
                      ctx->recipe_->spec.identity.c_str(),
                      identity);
  }

  // Look up dependency in recipe's dependency map
  recipe const *dep{ [&]() -> recipe const * {
    auto it{ ctx->recipe_->dependencies.find(identity) };
    if (it == ctx->recipe_->dependencies.end()) {
      luaL_error(lua, "ctx.asset: dependency not found in map: %s", identity);
    }
    return it->second.recipe_ptr;
  }() };

  if (!dep) { return luaL_error(lua, "ctx.asset: null dependency pointer: %s", identity); }

  std::string const path{ dep->asset_path.string() };
  lua_pushstring(lua, path.c_str());
  return 1;
}

}  // namespace envy
