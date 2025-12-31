#include "lua_envy_deps.h"

#include "engine.h"
#include "lua_phase_context.h"
#include "pkg.h"
#include "pkg_phase.h"
#include "product_util.h"
#include "trace.h"

#include <stdexcept>
#include <string>
#include <unordered_set>

namespace envy {
namespace {

std::string phase_name_str(pkg_phase p) { return std::string(pkg_phase_name(p)); }

bool dependency_reachable(pkg *from,
                          std::string const &target_identity,
                          std::unordered_set<pkg *> &visited) {
  if (!visited.insert(from).second) { return false; }

  for (auto const &[dep_id, dep_info] : from->dependencies) {
    pkg *child{ dep_info.p };
    if (!child) { continue; }
    if (dep_id == target_identity) { return true; }
    if (dependency_reachable(child, target_identity, visited)) { return true; }
  }

  return false;
}

bool strong_reachable(pkg *from,
                      std::string const &target_identity,
                      pkg_phase &first_hop_needed_by) {
  bool found{ false };

  for (auto const &[dep_id, dep_info] : from->dependencies) {
    pkg *child{ dep_info.p };
    if (!child) { continue; }

    bool reachable{ dep_id == target_identity };
    if (!reachable) {
      std::unordered_set<pkg *> visited;
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
  // envy.package(identity) -> path_string
  envy_table["package"] = [](std::string const &identity,
                             sol::this_state L) -> std::string {
    phase_context const *ctx{ lua_phase_context_get(L) };
    pkg *consumer{ ctx ? ctx->p : nullptr };
    if (!consumer) {
      throw std::runtime_error("envy.package: not in phase context (missing pkg)");
    }

    pkg_execution_ctx *exec_ctx{ consumer->exec_ctx };
    if (!exec_ctx) {
      throw std::runtime_error("envy.package: missing execution context for pkg '" +
                               consumer->cfg->identity + "'");
    }

    pkg_phase const current_phase{ exec_ctx->current_phase.load() };

    auto emit_access = [&](bool allowed, pkg_phase needed_by, std::string const &reason) {
      ENVY_TRACE_LUA_CTX_PACKAGE_ACCESS(consumer->cfg->identity,
                                        identity,
                                        current_phase,
                                        needed_by,
                                        allowed,
                                        reason);
    };

    pkg_phase first_needed_by{ pkg_phase::completion };
    if (!strong_reachable(consumer, identity, first_needed_by)) {
      std::string const msg{ "envy.package: pkg '" + consumer->cfg->identity +
                             "' has no strong dependency on '" + identity + "'" };
      emit_access(false, pkg_phase::none, msg);
      throw std::runtime_error(msg);
    }

    if (current_phase < first_needed_by) {
      std::string const msg{ "envy.package: dependency '" + identity + "' needed_by '" +
                             phase_name_str(first_needed_by) + "' but accessed during '" +
                             phase_name_str(current_phase) + "'" };
      emit_access(false, first_needed_by, msg);
      throw std::runtime_error(msg);
    }

    // Look up dependency in pkg's dependency map
    auto it{ consumer->dependencies.find(identity) };
    if (it == consumer->dependencies.end()) {
      std::string const msg{ "envy.package: dependency not found in map: " + identity };
      emit_access(false, first_needed_by, msg);
      throw std::runtime_error(msg);
    }

    pkg const *dep{ it->second.p };
    if (!dep) {
      std::string const msg{ "envy.package: null dependency pointer: " + identity };
      emit_access(false, first_needed_by, msg);
      throw std::runtime_error(msg);
    }

    if (dep->type == pkg_type::USER_MANAGED) {
      std::string const msg{ "envy.package: dependency '" + identity +
                             "' is user-managed and has no pkg path" };
      emit_access(false, first_needed_by, msg);
      throw std::runtime_error(msg);
    }

    if (dep->pkg_path.empty()) {
      std::string const msg{ "envy.package: dependency '" + identity +
                             "' has no pkg path (phase ordering issue?)" };
      emit_access(false, first_needed_by, msg);
      throw std::runtime_error(msg);
    }

    std::string const pkg_path{ dep->pkg_path.string() };
    emit_access(true, first_needed_by, pkg_path);
    return pkg_path;
  };

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
      std::string const msg{ "envy.product: product '" + product_name + "' needed_by '" +
                             phase_name_str(dep.needed_by) + "' but accessed during '" +
                             phase_name_str(current_phase) + "'" };
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
