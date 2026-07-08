#include "phase_setup.h"

#include "blake3_util.h"
#include "cache.h"
#include "engine.h"
#include "lua_ctx/lua_phase_context.h"
#include "lua_envy.h"
#include "lua_error_formatter.h"
#include "pkg.h"
#include "pkg_cfg.h"
#include "platform.h"
#include "shell.h"
#include "trace.h"
#include "tui.h"
#include "tui_actions.h"
#include "util.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

namespace envy {

namespace {

// pkg_dir argument for pair verbs: the installed payload path for cache-managed
// packages, nil for user-managed (no payload exists).
sol::object make_pkg_dir_arg(sol::state_view lua, pkg *p) {
  return (p->type == pkg_type::CACHE_MANAGED && !p->pkg_path.empty())
             ? sol::object{ lua, sol::in_place, util_path_with_separator(p->pkg_path) }
             : sol::object{ sol::lua_nil };
}

// Run a pair CHECK shell command; exit 0 = satisfied. Non-zero is normal flow
// (work needed), not an error.
bool run_pair_check_command(pkg *p, std::string_view cmd, std::string const &context) {
  shell_run_cfg cfg;
  cfg.env = shell_getenv();
  cfg.on_stdout_line = [](std::string_view) {};
  cfg.on_stderr_line = [](std::string_view) {};
  cfg.cwd = pkg_cfg::compute_project_root(p->cfg);
  cfg.shell = shell_resolve_default(p->default_shell_ptr);

  shell_result const result{ [&] {
    try {
      return shell_run(cmd, cfg);
    } catch (std::exception const &e) {
      throw std::runtime_error(context + " command failed for " + p->cfg->identity + ": " +
                               e.what());
    }
  }() };

  tui::debug("phase setup: %s shell check exit_code=%d (%s)",
             context.c_str(),
             result.exit_code,
             result.exit_code == 0 ? "satisfied" : "not satisfied");
  return result.exit_code == 0;
}

}  // namespace

// Run a pair's CHECK verb. Returns true when the host state is already satisfied.
// Function form: CHECK(pkg_dir, options) -> boolean | string (string runs as shell).
// Not in anonymous namespace so tests can call it.
bool run_pair_check(pkg *p, engine &eng, std::string const &name) {
  std::string const context{ "SETUP." + name + ".CHECK" };

  auto const lua_acc{ p->lua.lock() };
  sol::state_view lua{ *lua_acc };
  sol::object verb{ lua["SETUP"][name]["CHECK"] };

  if (verb.is<std::string>()) {
    return run_pair_check_command(p, verb.as<std::string>(), context);
  }

  if (!verb.is<sol::protected_function>()) {
    throw std::runtime_error(context + " must be a function or string for " +
                             p->cfg->identity);
  }

  std::filesystem::path const project_root{ pkg_cfg::compute_project_root(p->cfg) };
  phase_context_guard ctx_guard{ &eng, p, lua.lua_state(), project_root };
  sol::object opts{ lua.registry()[ENVY_OPTIONS_RIDX] };

  sol::object result_obj{ call_lua_function_with_enriched_errors(p, context, [&]() {
    return verb.as<sol::protected_function>()(make_pkg_dir_arg(lua, p), opts);
  }) };

  if (result_obj.is<bool>()) { return result_obj.as<bool>(); }
  if (result_obj.is<std::string>()) {
    return run_pair_check_command(p, result_obj.as<std::string>(), context);
  }

  throw std::runtime_error(context + " for " + p->cfg->identity +
                           " must return boolean or string, got " +
                           sol::type_name(lua.lua_state(), result_obj.get_type()));
}

// Run a pair's INSTALL verb against the host (cwd = project_root).
// Function form: INSTALL(pkg_dir, options) -> nil | string (string runs as shell).
// Not in anonymous namespace so tests can call it.
void run_pair_install(pkg *p, engine &eng, std::string const &name) {
  std::string const context{ "SETUP." + name + ".INSTALL" };
  std::filesystem::path const project_root{ pkg_cfg::compute_project_root(p->cfg) };

  auto const lua_acc{ p->lua.lock() };
  sol::state_view lua{ *lua_acc };
  sol::object verb{ lua["SETUP"][name]["INSTALL"] };

  auto const run_script{ [&](std::string_view script) {
    tui_actions::run_phase_shell_script(script,
                                        "Setup",
                                        project_root,
                                        p->cfg->identity,
                                        shell_resolve_default(p->default_shell_ptr),
                                        p->tui_section,
                                        eng.cache_root());
  } };

  if (verb.is<std::string>()) {
    run_script(verb.as<std::string>());
    return;
  }

  if (!verb.is<sol::protected_function>()) {
    throw std::runtime_error(context + " must be a function or string for " +
                             p->cfg->identity);
  }

  phase_context_guard ctx_guard{ &eng, p, lua.lua_state(), project_root };
  sol::object opts{ lua.registry()[ENVY_OPTIONS_RIDX] };

  sol::object result_obj{ call_lua_function_with_enriched_errors(p, context, [&]() {
    return verb.as<sol::protected_function>()(make_pkg_dir_arg(lua, p), opts);
  }) };

  sol::type const result_type{ result_obj.get_type() };
  if (result_type == sol::type::none || result_type == sol::type::lua_nil) { return; }
  if (!result_obj.is<std::string>()) {
    throw std::runtime_error(context + " for " + p->cfg->identity +
                             " must return nil or string, got " +
                             sol::type_name(lua.lua_state(), result_type));
  }
  run_script(result_obj.as<std::string>());
}

// Effective selection: explicit names from manifest entries, plus all pairs when
// any referrer used the default and the package is user-managed. Sorted for
// deterministic execution; unknown names are hard errors.
// Not in anonymous namespace so tests can call it.
std::vector<std::string> compute_selected_pairs(pkg *p) {
  std::unordered_set<std::string> selected;
  bool default_requested{ false };
  {
    std::lock_guard const deps_lock(p->deps_mutex);
    selected = p->setup_selected;
    default_requested = p->setup_default;
  }

  if (default_requested && p->type == pkg_type::USER_MANAGED) {
    for (auto const &[name, _] : p->setup_pairs) { selected.insert(name); }
  }

  for (auto const &name : selected) {
    if (!p->setup_pairs.contains(name)) {
      throw std::runtime_error(
          "Unknown setup pair '" + name + "' selected for " + p->cfg->identity +
          " (spec defines " +
          (p->setup_pairs.empty() ? "no SETUP pairs" : "different SETUP pair names") +
          ")");
    }
  }

  std::vector<std::string> sorted{ selected.begin(), selected.end() };
  std::ranges::sort(sorted);
  return sorted;
}

namespace {

// One pair: double-check lock pattern shared with legacy user-managed packages.
// The lock entry is ephemeral (mark_user_managed) — purged on release, never
// marked complete; the CHECK verb is the only re-run gate.
void run_setup_pair(pkg *p, engine &eng, std::string const &name) {
  auto const &pair_platforms{ p->setup_pairs.at(name) };
  if (!pair_platforms.empty() &&
      !util_platform_matches(pair_platforms, platform::os_name(), platform::arch_name())) {
    tui::debug("phase setup: [%s] pair '%s' skipped (platform mismatch)",
               p->cfg->identity.c_str(),
               name.c_str());
    return;
  }

  if (run_pair_check(p, eng, name)) {
    tui::debug("phase setup: [%s] pair '%s' satisfied (pre-lock)",
               p->cfg->identity.c_str(),
               name.c_str());
    return;
  }

  // Serialize concurrent envy processes on this (package, pair). The Lua lock is
  // never held across this acquisition (lock order: cache lock, then p->lua).
  std::string const lock_key{ p->cfg->format_key() + "|setup:" + name };
  auto const digest{ blake3_hash(lock_key.data(), lock_key.size()) };
  std::string const hash_prefix{ util_bytes_to_hex(digest.data(), 8) };

  auto cache_result{ p->cache_ptr->ensure_pkg(p->cfg->identity,
                                              platform::os_name(),
                                              platform::arch_name(),
                                              hash_prefix) };
  if (!cache_result.lock) {
    // Pair entries are always purged on release; a completed entry means a
    // stale/corrupt cache. Mirror legacy user-managed behavior: warn and skip.
    tui::warn("phase setup: [%s] unexpected completed cache entry for pair '%s' at %s",
              p->cfg->identity.c_str(),
              name.c_str(),
              cache_result.pkg_path.string().c_str());
    return;
  }
  cache_result.lock->mark_user_managed();

  if (run_pair_check(p, eng, name)) {  // Race: another process completed the work
    tui::debug("phase setup: [%s] pair '%s' satisfied (post-lock)",
               p->cfg->identity.c_str(),
               name.c_str());
    return;  // Lock destructor purges the ephemeral entry
  }

  tui::debug("phase setup: [%s] running pair '%s' install",
             p->cfg->identity.c_str(),
             name.c_str());
  run_pair_install(p, eng, name);
}

}  // namespace

void run_setup_phase(pkg *p, engine &eng) {
  phase_trace_scope const phase_scope{ p->cfg->identity,
                                       pkg_phase::pkg_setup,
                                       std::chrono::steady_clock::now() };

  if (p->type != pkg_type::CACHE_MANAGED && p->type != pkg_type::USER_MANAGED) { return; }

  for (auto const &name : compute_selected_pairs(p)) { run_setup_pair(p, eng, name); }
}

}  // namespace envy
