#include "lua_envy_package.h"

#include "engine.h"
#include "lua_envy_dep_util.h"
#include "lua_phase_context.h"
#include "pkg.h"
#include "pkg_phase.h"
#include "trace.h"

#include <stdexcept>
#include <string>

namespace envy {

void lua_envy_package_install(sol::table &envy_table) {
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
      std::string const msg{
        "envy.package: dependency '" + identity + "' needed_by '" +
        std::string(pkg_phase_name(first_needed_by)) + "' but accessed during '" +
        std::string(pkg_phase_name(current_phase)) + "'"
      };
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
}

}  // namespace envy
