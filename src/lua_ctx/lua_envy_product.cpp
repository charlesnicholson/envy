#include "lua_envy_product.h"

#include "engine.h"
#include "lua_phase_context.h"
#include "pkg.h"
#include "pkg_phase.h"
#include "product_util.h"
#include "trace.h"

#include <stdexcept>
#include <string>

namespace envy {

void lua_envy_product_install(sol::table &envy_table) {
  // envy.product(name) -> path_or_value_string
  envy_table["product"] = [](std::string const &product_name,
                             sol::this_state L) -> std::string {
    phase_context const *ctx{ lua_phase_context_get(L) };
    pkg *consumer{ ctx ? ctx->p : nullptr };
    if (!consumer) {
      throw std::runtime_error("envy.product: not in phase context (missing pkg)");
    }
    if (product_name.empty()) {
      throw std::runtime_error("envy.product: product name cannot be empty");
    }

    pkg_execution_ctx *exec_ctx{ consumer->exec_ctx };
    if (!exec_ctx) {
      throw std::runtime_error("envy.product: missing execution context for pkg '" +
                               consumer->cfg->identity + "'");
    }

    pkg_phase const current_phase{ exec_ctx->current_phase.load() };

    auto const dep_it{ consumer->product_dependencies.find(product_name) };
    if (dep_it == consumer->product_dependencies.end()) {
      std::string const msg{ "envy.product: pkg '" + consumer->cfg->identity +
                             "' does not declare product dependency on '" + product_name +
                             "'" };
      ENVY_TRACE_LUA_CTX_PRODUCT_ACCESS(consumer->cfg->identity,
                                        product_name,
                                        "",
                                        current_phase,
                                        pkg_phase::none,
                                        false,
                                        msg);
      throw std::runtime_error(msg);
    }

    pkg::product_dependency const &dep{ dep_it->second };

    auto emit_access = [&](bool allowed, std::string const &reason) {
      std::string const provider_identity{ dep.provider ? dep.provider->cfg->identity
                                                        : std::string{} };
      ENVY_TRACE_LUA_CTX_PRODUCT_ACCESS(consumer->cfg->identity,
                                        product_name,
                                        provider_identity,
                                        current_phase,
                                        dep.needed_by,
                                        allowed,
                                        reason);
    };

    if (current_phase < dep.needed_by) {
      std::string const msg{
        "envy.product: product '" + product_name + "' needed_by '" +
        std::string(pkg_phase_name(dep.needed_by)) + "' but accessed during '" +
        std::string(pkg_phase_name(current_phase)) + "'"
      };
      emit_access(false, msg);
      throw std::runtime_error(msg);
    }

    if (!dep.provider) {
      std::string const msg{ "envy.product: product '" + product_name +
                             "' provider not resolved for pkg '" +
                             consumer->cfg->identity + "'" };
      emit_access(false, msg);
      throw std::runtime_error(msg);
    }

    if (!dep.constraint_identity.empty() &&
        dep.provider->cfg->identity != dep.constraint_identity) {
      std::string const msg{ "envy.product: product '" + product_name +
                             "' must come from '" + dep.constraint_identity +
                             "', but provider is '" + dep.provider->cfg->identity + "'" };
      emit_access(false, msg);
      throw std::runtime_error(msg);
    }

    std::string const value{ product_util_resolve(dep.provider, product_name) };
    emit_access(true, value);
    return value;
  };
}

}  // namespace envy
