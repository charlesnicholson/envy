#include "lua_ctx_bindings.h"

#include "product_util.h"
#include "engine.h"
#include "recipe.h"
#include "recipe_phase.h"
#include "trace.h"

#include <functional>
#include <stdexcept>
#include <string>

namespace envy {

namespace {

std::string phase_name_str(recipe_phase p) { return std::string(recipe_phase_name(p)); }

}  // namespace

std::function<std::string(std::string const &)> make_ctx_product(lua_ctx_common *ctx) {
  return [ctx](std::string const &product_name) -> std::string {
    if (!ctx->recipe_) { throw std::runtime_error("ctx.product: missing recipe context"); }
    if (product_name.empty()) {
      throw std::runtime_error("ctx.product: product name cannot be empty");
    }

    recipe *const consumer{ ctx->recipe_ };
    recipe_execution_ctx *exec_ctx{ consumer->exec_ctx };
    if (!exec_ctx) {
      throw std::runtime_error("ctx.product: missing execution context for recipe '" +
                               consumer->spec->identity + "'");
    }

    recipe_phase const current_phase{ exec_ctx->current_phase.load() };

    auto const dep_it{ consumer->product_dependencies.find(product_name) };
    if (dep_it == consumer->product_dependencies.end()) {
      std::string const msg{ "ctx.product: recipe '" + consumer->spec->identity +
                             "' does not declare product dependency on '" +
                             product_name + "'" };
      ENVY_TRACE_LUA_CTX_PRODUCT_ACCESS(consumer->spec->identity,
                                        product_name,
                                        "",
                                        current_phase,
                                        recipe_phase::none,
                                        false,
                                        msg);
      throw std::runtime_error(msg);
    }

    recipe::product_dependency const &dep{ dep_it->second };

    auto emit_access = [&](bool allowed, std::string const &reason) {
      std::string const provider_identity{ dep.provider ? dep.provider->spec->identity
                                                        : std::string{} };
      ENVY_TRACE_LUA_CTX_PRODUCT_ACCESS(consumer->spec->identity,
                                        product_name,
                                        provider_identity,
                                        current_phase,
                                        dep.needed_by,
                                        allowed,
                                        reason);
    };

    if (current_phase < dep.needed_by) {
      std::string const msg{ "ctx.product: product '" + product_name +
                             "' needed_by '" + phase_name_str(dep.needed_by) +
                             "' but accessed during '" + phase_name_str(current_phase) +
                             "'" };
      emit_access(false, msg);
      throw std::runtime_error(msg);
    }

    if (!dep.provider) {
      std::string const msg{ "ctx.product: product '" + product_name +
                             "' provider not resolved for recipe '" +
                             consumer->spec->identity + "'" };
      emit_access(false, msg);
      throw std::runtime_error(msg);
    }

    if (!dep.constraint_identity.empty() &&
        dep.provider->spec->identity != dep.constraint_identity) {
      std::string const msg{ "ctx.product: product '" + product_name +
                             "' must come from '" + dep.constraint_identity +
                             "', but provider is '" + dep.provider->spec->identity +
                             "'" };
      emit_access(false, msg);
      throw std::runtime_error(msg);
    }

    std::string const value{ product_util_resolve(dep.provider, product_name) };
    emit_access(true, value);
    return value;
  };
}

}  // namespace envy
