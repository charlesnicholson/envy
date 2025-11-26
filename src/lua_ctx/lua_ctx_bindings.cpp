#include "lua_ctx_bindings.h"

#include "recipe.h"

#include <string>

namespace envy {

// Check if target_identity is a declared dependency of current recipe
// Used for ctx.asset() validation. Exposed for testing.
bool is_declared_dependency(recipe *r, std::string const &target_identity) {
  for (std::string const &dep_identity : r->declared_dependencies) {
    if (dep_identity == target_identity) { return true; }
  }
  return false;
}

void lua_ctx_add_common_bindings(sol::table &ctx_table, lua_ctx_common *ctx) {
  ctx_table["copy"] = make_ctx_copy(ctx);
  ctx_table["move"] = make_ctx_move(ctx);
  ctx_table["extract"] = make_ctx_extract(ctx);
  ctx_table["extract_all"] = make_ctx_extract_all(ctx);
  ctx_table["asset"] = make_ctx_asset(ctx);
  ctx_table["ls"] = make_ctx_ls(ctx);
  ctx_table["run"] = make_ctx_run(ctx);
}

void lua_ctx_bindings_register_fetch_phase(lua_State *lua, fetch_phase_ctx *context) {
  sol::state_view lua_view{ lua };
  sol::table ctx_table{ lua_view, sol::stack_reference(lua, -1) };

  ctx_table["fetch"] = make_ctx_fetch(context);
  ctx_table["commit_fetch"] = make_ctx_commit_fetch(context);
}

}  // namespace envy
