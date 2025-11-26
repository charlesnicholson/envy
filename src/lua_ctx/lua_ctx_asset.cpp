#include "lua_ctx_bindings.h"

#include "recipe.h"

#include <functional>
#include <string>

namespace envy {

std::function<std::string(std::string const &)> make_ctx_asset(lua_ctx_common *ctx) {
  return [ctx](std::string const &identity) -> std::string {
    if (!ctx->recipe_) { throw std::runtime_error("ctx.asset: missing recipe context"); }

    // Validate dependency declaration (only checks direct dependencies)
    if (!is_declared_dependency(ctx->recipe_, identity)) {
      throw std::runtime_error("ctx.asset: recipe '" + ctx->recipe_->spec->identity +
                               "' does not declare dependency on '" + identity + "'");
    }

    // Look up dependency in recipe's dependency map
    auto it{ ctx->recipe_->dependencies.find(identity) };
    if (it == ctx->recipe_->dependencies.end()) {
      throw std::runtime_error("ctx.asset: dependency not found in map: " + identity);
    }

    recipe const *dep{ it->second.recipe_ptr };
    if (!dep) {
      throw std::runtime_error("ctx.asset: null dependency pointer: " + identity);
    }

    return dep->asset_path.string();
  };
}

}  // namespace envy
