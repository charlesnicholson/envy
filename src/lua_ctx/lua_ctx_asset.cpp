#include "lua_ctx_bindings.h"

#include "engine.h"
#include "recipe.h"
#include "recipe_phase.h"
#include "trace.h"

#include <functional>
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

std::function<std::string(std::string const &)> make_ctx_asset(lua_ctx_common *ctx) {
  return [ctx](std::string const &identity) -> std::string {
    if (!ctx->recipe_) { throw std::runtime_error("ctx.asset: missing recipe context"); }

    recipe *consumer{ ctx->recipe_ };
    recipe_execution_ctx *exec_ctx{ consumer->exec_ctx };
    if (!exec_ctx) {
      throw std::runtime_error("ctx.asset: missing execution context for recipe '" +
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
      std::string const msg{ "ctx.asset: recipe '" + consumer->spec->identity +
                             "' has no strong dependency on '" + identity + "'" };
      emit_access(false, recipe_phase::none, msg);
      throw std::runtime_error(msg);
    }

    if (current_phase < first_needed_by) {
      std::string const msg{ "ctx.asset: dependency '" + identity + "' needed_by '" +
                             phase_name_str(first_needed_by) + "' but accessed during '" +
                             phase_name_str(current_phase) + "'" };
      emit_access(false, first_needed_by, msg);
      throw std::runtime_error(msg);
    }

    // Look up dependency in recipe's dependency map
    auto it{ consumer->dependencies.find(identity) };
    if (it == consumer->dependencies.end()) {
      std::string const msg{ "ctx.asset: dependency not found in map: " + identity };
      emit_access(false, first_needed_by, msg);
      throw std::runtime_error(msg);
    }

    recipe const *dep{ it->second.recipe_ptr };
    if (!dep) {
      std::string const msg{ "ctx.asset: null dependency pointer: " + identity };
      emit_access(false, first_needed_by, msg);
      throw std::runtime_error(msg);
    }

    if (dep->type == recipe_type::USER_MANAGED) {
      std::string const msg{ "ctx.asset: dependency '" + identity +
                             "' is user-managed and has no asset path" };
      emit_access(false, first_needed_by, msg);
      throw std::runtime_error(msg);
    }

    if (dep->asset_path.empty()) {
      std::string const msg{ "ctx.asset: dependency '" + identity +
                             "' has no asset path (phase ordering issue?)" };
      emit_access(false, first_needed_by, msg);
      throw std::runtime_error(msg);
    }

    std::string const asset_path{ dep->asset_path.string() };
    emit_access(true, first_needed_by, asset_path);
    return asset_path;
  };
}

}  // namespace envy
