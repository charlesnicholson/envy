#include "lua_ctx_bindings.h"

#include "engine.h"
#include "pkg.h"
#include "pkg_phase.h"
#include "trace.h"

#include <functional>
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

std::function<std::string(std::string const &)> make_ctx_asset(lua_ctx_common *ctx) {
  return [ctx](std::string const &identity) -> std::string {
    if (!ctx->pkg_) { throw std::runtime_error("ctx.asset: missing pkg context"); }

    pkg *consumer{ ctx->pkg_ };
    pkg_execution_ctx *exec_ctx{ consumer->exec_ctx };
    if (!exec_ctx) {
      throw std::runtime_error("ctx.asset: missing execution context for pkg '" +
                               consumer->cfg->identity + "'");
    }

    pkg_phase const current_phase{ exec_ctx->current_phase.load() };

    auto const trace_access{
      [&](bool allowed, pkg_phase needed_by, std::string const &reason) {
        ENVY_TRACE_LUA_CTX_ASSET_ACCESS(consumer->cfg->identity,
                                        identity,
                                        current_phase,
                                        needed_by,
                                        allowed,
                                        reason);
      }
    };

    pkg_phase first_needed_by{ pkg_phase::completion };
    if (!strong_reachable(consumer, identity, first_needed_by)) {
      std::string const msg{ "ctx.asset: pkg '" + consumer->cfg->identity +
                             "' has no strong dependency on '" + identity + "'" };
      trace_access(false, pkg_phase::none, msg);
      throw std::runtime_error(msg);
    }

    if (current_phase < first_needed_by) {
      std::string const msg{ "ctx.asset: dependency '" + identity + "' needed_by '" +
                             phase_name_str(first_needed_by) + "' but accessed during '" +
                             phase_name_str(current_phase) + "'" };
      trace_access(false, first_needed_by, msg);
      throw std::runtime_error(msg);
    }

    pkg const *dep{ [&] {  // Look up dependency in pkg's dependency map
      auto it{ consumer->dependencies.find(identity) };
      if (it == consumer->dependencies.end()) {
        std::string const msg{ "ctx.asset: dependency not found in map: " + identity };
        trace_access(false, first_needed_by, msg);
        throw std::runtime_error(msg);
      }
      return it->second.p;
    }() };

    if (!dep) {
      std::string const msg{ "ctx.asset: null dependency pointer: " + identity };
      trace_access(false, first_needed_by, msg);
      throw std::runtime_error(msg);
    }

    if (dep->type == pkg_type::USER_MANAGED) {
      std::string const msg{ "ctx.asset: dependency '" + identity +
                             "' is user-managed and has no pkg path" };
      trace_access(false, first_needed_by, msg);
      throw std::runtime_error(msg);
    }

    if (dep->pkg_path.empty()) {
      std::string const msg{ "ctx.asset: dependency '" + identity +
                             "' has no pkg path (phase ordering issue?)" };
      trace_access(false, first_needed_by, msg);
      throw std::runtime_error(msg);
    }

    std::string const pkg_path{ dep->pkg_path.string() };
    trace_access(true, first_needed_by, pkg_path);
    return pkg_path;
  };
}

}  // namespace envy
