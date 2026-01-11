#include "lua_envy_loadenv_spec.h"

#include "bundle.h"
#include "engine.h"
#include "lua_envy_dep_util.h"
#include "lua_phase_context.h"
#include "pkg.h"
#include "pkg_phase.h"
#include "trace.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>

namespace envy {

namespace {

// Convert Lua module path (dots) to filesystem path (slashes)
std::string module_path_to_file_path(std::string const &module_path) {
  std::string file_path{ module_path };
  std::replace(file_path.begin(), file_path.end(), '.', '/');
  return file_path;
}

}  // namespace

void lua_envy_loadenv_spec_install(sol::table &envy_table) {
  // envy.loadenv_spec(identity, module_path) -> table
  // Load Lua code from a declared dependency into a sandboxed environment
  // module_path uses Lua dot syntax (e.g., "lib.helpers" -> "lib/helpers.lua")
  envy_table["loadenv_spec"] = [](std::string const &identity,
                                  std::string const &module_path,
                                  sol::this_state L) -> sol::table {
    // Convert dots to slashes
    std::string const subpath{ module_path_to_file_path(module_path) };
    // Verify we're in a phase context (not global scope)
    phase_context const *ctx{ lua_phase_context_get(L) };
    pkg *consumer{ ctx ? ctx->p : nullptr };
    if (!consumer) {
      throw std::runtime_error(
          "envy.loadenv_spec: can only be called within phase functions, not at global "
          "scope");
    }

    engine *eng{ ctx->eng };
    if (!eng) { throw std::runtime_error("envy.loadenv_spec: missing engine context"); }

    pkg_execution_ctx *exec_ctx{ consumer->exec_ctx };
    if (!exec_ctx) {
      throw std::runtime_error("envy.loadenv_spec: missing execution context for pkg '" +
                               consumer->cfg->identity + "'");
    }

    pkg_phase const current_phase{ exec_ctx->current_phase.load() };

    auto emit_access = [&](bool allowed, pkg_phase needed_by, std::string const &reason) {
      ENVY_TRACE_LUA_CTX_LOADENV_SPEC_ACCESS(consumer->cfg->identity,
                                             identity,
                                             module_path,  // Log original dot syntax
                                             current_phase,
                                             needed_by,
                                             allowed,
                                             reason);
    };

    // Look up dependency by identity (with fuzzy matching)
    pkg_phase first_needed_by{ pkg_phase::completion };
    std::optional<std::string> matched_identity;
    if (!strong_reachable(consumer, identity, first_needed_by, matched_identity)) {
      std::string const msg{ "envy.loadenv_spec: pkg '" + consumer->cfg->identity +
                             "' has no dependency on '" + identity + "'" };
      emit_access(false, pkg_phase::none, msg);
      throw std::runtime_error(msg);
    }

    if (current_phase < first_needed_by) {
      std::string const msg{ "envy.loadenv_spec: dependency '" + identity +
                             "' needed_by '" +
                             std::string(pkg_phase_name(first_needed_by)) +
                             "' but accessed during '" +
                             std::string(pkg_phase_name(current_phase)) + "'" };
      emit_access(false, first_needed_by, msg);
      throw std::runtime_error(msg);
    }

    // Use canonical identity from fuzzy match for lookup
    std::string const &canonical_id{ matched_identity.value_or(identity) };

    // Find the dependency package
    auto it{ consumer->dependencies.find(canonical_id) };
    if (it == consumer->dependencies.end()) {
      throw std::runtime_error("envy.loadenv_spec: dependency not found in map: " +
                               canonical_id);
    }

    pkg const *dep{ it->second.p };
    if (!dep) {
      throw std::runtime_error("envy.loadenv_spec: null dependency pointer: " + identity);
    }

    // Determine load root path based on dependency type
    std::filesystem::path load_root;

    if (dep->type == pkg_type::BUNDLE_ONLY) {
      // Pure bundle dependency - use bundle's cache_path
      bundle *b{ eng->find_bundle(canonical_id) };
      if (!b) {
        throw std::runtime_error("envy.loadenv_spec: bundle '" + canonical_id +
                                 "' not found in registry");
      }
      load_root = b->cache_path;
    } else if (dep->cfg->bundle_identity.has_value()) {
      // Spec from bundle - use the containing bundle's cache_path
      bundle *b{ eng->find_bundle(*dep->cfg->bundle_identity) };
      if (!b) {
        throw std::runtime_error("envy.loadenv_spec: bundle '" +
                                 *dep->cfg->bundle_identity + "' not found for spec '" +
                                 identity + "'");
      }
      load_root = b->cache_path;
    } else {
      // Atomic spec - use spec's cache directory
      if (!dep->spec_file_path.has_value() || dep->spec_file_path->empty()) {
        throw std::runtime_error("envy.loadenv_spec: spec '" + identity +
                                 "' has no spec_file_path");
      }
      load_root = dep->spec_file_path->parent_path();
    }

    // Construct full path (add .lua extension)
    std::filesystem::path const full_path{ load_root / (subpath + ".lua") };

    if (!std::filesystem::exists(full_path)) {
      throw std::runtime_error("envy.loadenv_spec: file not found: " + full_path.string());
    }

    // Load file content
    std::ifstream ifs{ full_path };
    if (!ifs) {
      throw std::runtime_error("envy.loadenv_spec: failed to open: " + full_path.string());
    }
    std::ostringstream oss;
    oss << ifs.rdbuf();
    std::string const content{ oss.str() };

    // Create sandboxed environment with access to stdlib via _G (metatable fallback)
    sol::state_view lua{ L };
    sol::environment env{ lua, sol::create, lua.globals() };

    // Load chunk
    sol::load_result chunk{ lua.load(content, full_path.string()) };
    if (!chunk.valid()) {
      sol::error err{ chunk };
      throw std::runtime_error("envy.loadenv_spec: load error: " +
                               std::string(err.what()));
    }

    // Set environment on the function
    sol::protected_function fn{ chunk };
    sol::set_environment(env, fn);

    // Execute with our environment
    sol::protected_function_result result{ fn() };
    if (!result.valid()) {
      sol::error err{ result };
      throw std::runtime_error("envy.loadenv_spec: exec error: " +
                               std::string(err.what()));
    }

    emit_access(true, first_needed_by, full_path.string());
    return env;
  };
}

}  // namespace envy
