#include "lua_envy_deps.h"

#include "engine.h"
#include "lua_phase_context.h"
#include "product_util.h"
#include "recipe.h"
#include "recipe_phase.h"
#include "trace.h"

#include <stdexcept>
#include <string>
#include <unordered_set>

namespace envy {
namespace {

std::string phase_name_str(recipe_phase p) { return std::string(recipe_phase_name(p)); }

bool dependency_reachable(recipe *from,
                          std::string const &target_identity,
                          std::unordered_set<recipe *> &visited) {
  if (!visited.insert(from).second) { return false; }

  for (auto const &[dep_id, dep_info] : from->dependencies) {
    recipe *child{ dep_info.recipe_ptr };
    if (!child) { continue; }
    if (dep_id == target_identity) { return true; }
    if (dependency_reachable(child, target_identity, visited)) { return true; }
  }

  return false;
}

bool strong_reachable(recipe *from,
                      std::string const &target_identity,
                      recipe_phase &first_hop_needed_by) {
  bool found{ false };

  for (auto const &[dep_id, dep_info] : from->dependencies) {
    recipe *child{ dep_info.recipe_ptr };
    if (!child) { continue; }

    bool reachable{ dep_id == target_identity };
    if (!reachable) {
      std::unordered_set<recipe *> visited;
      reachable = dependency_reachable(child, target_identity, visited);
    }

    if (!reachable) { continue; }

    if (!found || dep_info.needed_by < first_hop_needed_by) {
      first_hop_needed_by = dep_info.needed_by;
    }
    found = true;
  }

  return found;
}

}  // namespace

void lua_envy_deps_install(sol::table &envy_table) {
  // envy.asset(identity) -> path_string
  envy_table["asset"] = [](std::string const &identity, sol::this_state L) -> std::string {
    recipe *consumer{ lua_phase_context_get_recipe(L) };
    if (!consumer) {
      throw std::runtime_error("envy.asset: not in phase context (missing recipe)");
    }

    recipe_execution_ctx *exec_ctx{ consumer->exec_ctx };
    if (!exec_ctx) {
      throw std::runtime_error("envy.asset: missing execution context for recipe '" +
                               consumer->spec->identity + "'");
    }

    recipe_phase const current_phase{ exec_ctx->current_phase.load() };

    auto emit_access =
        [&](bool allowed, recipe_phase needed_by, std::string const &reason) {
          ENVY_TRACE_LUA_CTX_ASSET_ACCESS(consumer->spec->identity,
                                          identity,
                                          current_phase,
                                          needed_by,
                                          allowed,
                                          reason);
        };

    recipe_phase first_needed_by{ recipe_phase::completion };
    if (!strong_reachable(consumer, identity, first_needed_by)) {
      std::string const msg{ "envy.asset: recipe '" + consumer->spec->identity +
                             "' has no strong dependency on '" + identity + "'" };
      emit_access(false, recipe_phase::none, msg);
      throw std::runtime_error(msg);
    }

    if (current_phase < first_needed_by) {
      std::string const msg{ "envy.asset: dependency '" + identity + "' needed_by '" +
                             phase_name_str(first_needed_by) + "' but accessed during '" +
                             phase_name_str(current_phase) + "'" };
      emit_access(false, first_needed_by, msg);
      throw std::runtime_error(msg);
    }

    // Look up dependency in recipe's dependency map
    auto it{ consumer->dependencies.find(identity) };
    if (it == consumer->dependencies.end()) {
      std::string const msg{ "envy.asset: dependency not found in map: " + identity };
      emit_access(false, first_needed_by, msg);
      throw std::runtime_error(msg);
    }

    recipe const *dep{ it->second.recipe_ptr };
    if (!dep) {
      std::string const msg{ "envy.asset: null dependency pointer: " + identity };
      emit_access(false, first_needed_by, msg);
      throw std::runtime_error(msg);
    }

    if (dep->type == recipe_type::USER_MANAGED) {
      std::string const msg{ "envy.asset: dependency '" + identity +
                             "' is user-managed and has no asset path" };
      emit_access(false, first_needed_by, msg);
      throw std::runtime_error(msg);
    }

    if (dep->asset_path.empty()) {
      std::string const msg{ "envy.asset: dependency '" + identity +
                             "' has no asset path (phase ordering issue?)" };
      emit_access(false, first_needed_by, msg);
      throw std::runtime_error(msg);
    }

    std::string const asset_path{ dep->asset_path.string() };
    emit_access(true, first_needed_by, asset_path);
    return asset_path;
  };

  // envy.product(name) -> path_or_value_string
  envy_table["product"] = [](std::string const &product_name,
                             sol::this_state L) -> std::string {
    recipe *consumer{ lua_phase_context_get_recipe(L) };
    if (!consumer) {
      throw std::runtime_error("envy.product: not in phase context (missing recipe)");
    }
    if (product_name.empty()) {
      throw std::runtime_error("envy.product: product name cannot be empty");
    }

    recipe_execution_ctx *exec_ctx{ consumer->exec_ctx };
    if (!exec_ctx) {
      throw std::runtime_error("envy.product: missing execution context for recipe '" +
                               consumer->spec->identity + "'");
    }

    recipe_phase const current_phase{ exec_ctx->current_phase.load() };

    auto const dep_it{ consumer->product_dependencies.find(product_name) };
    if (dep_it == consumer->product_dependencies.end()) {
      std::string const msg{ "envy.product: recipe '" + consumer->spec->identity +
                             "' does not declare product dependency on '" + product_name +
                             "'" };
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
      std::string const msg{ "envy.product: product '" + product_name + "' needed_by '" +
                             phase_name_str(dep.needed_by) + "' but accessed during '" +
                             phase_name_str(current_phase) + "'" };
      emit_access(false, msg);
      throw std::runtime_error(msg);
    }

    if (!dep.provider) {
      std::string const msg{ "envy.product: product '" + product_name +
                             "' provider not resolved for recipe '" +
                             consumer->spec->identity + "'" };
      emit_access(false, msg);
      throw std::runtime_error(msg);
    }

    if (!dep.constraint_identity.empty() &&
        dep.provider->spec->identity != dep.constraint_identity) {
      std::string const msg{ "envy.product: product '" + product_name +
                             "' must come from '" + dep.constraint_identity +
                             "', but provider is '" + dep.provider->spec->identity + "'" };
      emit_access(false, msg);
      throw std::runtime_error(msg);
    }

    std::string const value{ product_util_resolve(dep.provider, product_name) };
    emit_access(true, value);
    return value;
  };
}

}  // namespace envy
