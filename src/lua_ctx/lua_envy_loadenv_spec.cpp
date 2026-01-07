#include "lua_envy_loadenv_spec.h"

#include "bundle.h"
#include "engine.h"
#include "lua_envy_dep_util.h"
#include "lua_phase_context.h"
#include "pkg.h"
#include "pkg_phase.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace envy {

void lua_envy_loadenv_spec_install(sol::table &envy_table) {
  // envy.loadenv_spec(identity, subpath) -> table
  // Load Lua code from a declared dependency into a sandboxed environment
  envy_table["loadenv_spec"] = [](std::string const &identity,
                                  std::string const &subpath,
                                  sol::this_state L) -> sol::table {
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

    // Look up dependency by identity
    pkg_phase first_needed_by{ pkg_phase::completion };
    if (!strong_reachable(consumer, identity, first_needed_by)) {
      throw std::runtime_error("envy.loadenv_spec: pkg '" + consumer->cfg->identity +
                               "' has no dependency on '" + identity + "'");
    }

    if (current_phase < first_needed_by) {
      throw std::runtime_error(
          "envy.loadenv_spec: dependency '" + identity + "' needed_by '" +
          std::string(pkg_phase_name(first_needed_by)) + "' but accessed during '" +
          std::string(pkg_phase_name(current_phase)) + "'");
    }

    // Find the dependency package
    auto it{ consumer->dependencies.find(identity) };
    if (it == consumer->dependencies.end()) {
      throw std::runtime_error("envy.loadenv_spec: dependency not found in map: " +
                               identity);
    }

    pkg const *dep{ it->second.p };
    if (!dep) {
      throw std::runtime_error("envy.loadenv_spec: null dependency pointer: " + identity);
    }

    // Determine load root path based on dependency type
    std::filesystem::path load_root;

    if (dep->type == pkg_type::BUNDLE_ONLY) {
      // Pure bundle dependency - use bundle's cache_path
      bundle *b{ eng->find_bundle(identity) };
      if (!b) {
        throw std::runtime_error("envy.loadenv_spec: bundle '" + identity +
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

    // Create sandboxed environment with access to stdlib via _G
    sol::state_view lua{ L };
    sol::table env{ lua.create_table() };
    sol::table meta{ lua.create_table() };
    meta[sol::meta_function::index] = lua.globals();
    env[sol::metatable_key] = meta;

    // Load chunk
    sol::load_result chunk{ lua.load(content, full_path.string()) };
    if (!chunk.valid()) {
      sol::error err{ chunk };
      throw std::runtime_error("envy.loadenv_spec: load error: " +
                               std::string(err.what()));
    }

    // Set environment on the function (using Lua C API)
    sol::protected_function fn{ chunk };
    lua_State *lua_st{ lua.lua_state() };
    fn.push();
    env.push();
    lua_setupvalue(lua_st, -2, 1);  // Set first upvalue (_ENV) to our env table
    lua_pop(lua_st, 1);             // Pop function (it's already stored in fn)

    // Execute with our environment
    sol::protected_function_result result{ fn() };
    if (!result.valid()) {
      sol::error err{ result };
      throw std::runtime_error("envy.loadenv_spec: exec error: " +
                               std::string(err.what()));
    }

    return env;
  };
}

}  // namespace envy
