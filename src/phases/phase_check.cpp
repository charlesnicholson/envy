#include "phase_check.h"

#include "blake3_util.h"
#include "cache.h"
#include "engine.h"
#include "lua_ctx/lua_ctx_bindings.h"
#include "lua_envy.h"
#include "lua_error_formatter.h"
#include "recipe.h"
#include "recipe_spec.h"
#include "shell.h"
#include "trace.h"
#include "tui.h"
#include "util.h"

#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <utility>

namespace envy {

bool run_check_string(recipe *r, engine &eng, std::string_view check_cmd) {
  tui::debug("phase check: executing string check: %s", std::string(check_cmd).c_str());

  std::string stdout_capture;
  std::string stderr_capture;

  shell_run_cfg cfg;
  cfg.env = shell_getenv();
  cfg.on_stdout_line = [&](std::string_view line) {
    stdout_capture += line;
    stdout_capture += '\n';
  };
  cfg.on_stderr_line = [&](std::string_view line) {
    stderr_capture += line;
    stderr_capture += '\n';
  };
  cfg.cwd = recipe_spec::compute_project_root(r->spec);
  cfg.shell = shell_resolve_default(r ? r->default_shell_ptr : nullptr);

  shell_result result;
  try {
    result = shell_run(check_cmd, cfg);
  } catch (std::exception const &e) {
    throw std::runtime_error("check command failed for " + r->spec->identity + ": " +
                             e.what());
  }

  bool const check_passed{ result.exit_code == 0 };

  if (!check_passed) {
    tui::error("check failed for %s (exit code %d)",
               r->spec->identity.c_str(),
               result.exit_code);
    tui::error("command: %s", std::string(check_cmd).c_str());
    if (!stdout_capture.empty()) { tui::error("stdout:\n%s", stdout_capture.c_str()); }
    if (!stderr_capture.empty()) { tui::error("stderr:\n%s", stderr_capture.c_str()); }
  }

  tui::debug("phase check: string check exit_code=%d (check %s)",
             result.exit_code,
             check_passed ? "passed" : "failed");
  return check_passed;
}

bool run_check_function(recipe *r,
                        engine &eng,
                        sol::state_view lua,
                        sol::protected_function check_func) {
  tui::debug("phase check: executing function check");

  struct check_phase_ctx : lua_ctx_common {};

  check_phase_ctx ctx{};
  ctx.run_dir = recipe_spec::compute_project_root(r->spec);
  ctx.engine_ = nullptr;
  ctx.recipe_ = r;

  sol::table ctx_table{ lua.create_table() };
  ctx_table["identity"] = r->spec->identity;
  ctx_table["run"] = make_ctx_run(&ctx);

  sol::object opts{ lua.registry()[ENVY_OPTIONS_RIDX] };
  sol::object result_obj{ call_lua_function_with_enriched_errors(r, "check", [&]() {
    return check_func(ctx_table, opts);
  }) };

  // Check function can return:
  // 1. Boolean - true/false for check passed/failed
  // 2. String - execute as shell command and return result
  sol::type const result_type{ result_obj.get_type() };

  if (result_obj.is<bool>()) {
    bool const check_passed{ result_obj.as<bool>() };
    tui::debug("phase check: function check returned %s", check_passed ? "true" : "false");
    return check_passed;
  } else if (result_obj.is<std::string>()) {
    // Function returned a string - execute it as a shell command
    std::string const check_cmd{ result_obj.as<std::string>() };
    tui::debug("phase check: function check returned string, executing: %s",
               check_cmd.c_str());
    return run_check_string(r, eng, check_cmd);
  }

  throw std::runtime_error("check function for " + r->spec->identity +
                           " must return boolean or string, got " +
                           sol::type_name(lua.lua_state(), result_type));
}

bool run_check_verb(recipe *r, engine &eng, sol::state_view lua) {
  sol::object check_obj{ lua["check"] };

  if (check_obj.is<sol::protected_function>()) {
    return run_check_function(r, eng, lua, check_obj.as<sol::protected_function>());
  } else if (check_obj.is<std::string>()) {
    return run_check_string(r, eng, check_obj.as<std::string_view>());
  }

  return false;
}

// Helper: Check if recipe has check verb
bool recipe_has_check_verb(recipe *r, sol::state_view lua) {
  sol::object check_obj{ lua["check"] };
  return check_obj.is<sol::protected_function>() || check_obj.is<std::string>();
}

// Helper: Compute hash and perform cache lookup (common to both user/cache-managed)
cache::ensure_result compute_hash_and_lookup_cache(recipe *r, sol::state_view lua) {
  // Compute hash including resolved weak/ref-only dependencies
  std::string key_for_hash{ r->spec->format_key() };
  if (!r->resolved_weak_dependency_keys.empty()) {
    for (auto const &wk : r->resolved_weak_dependency_keys) { key_for_hash += "|" + wk; }
  }

  auto const digest{ blake3_hash(key_for_hash.data(), key_for_hash.size()) };
  r->canonical_identity_hash = util_bytes_to_hex(digest.data(), 32);
  std::string const hash_prefix{ util_bytes_to_hex(digest.data(), 8) };

  std::string const platform{ lua["ENVY_PLATFORM"].get<std::string>() };
  std::string const arch{ lua["ENVY_ARCH"].get<std::string>() };

  return r->cache_ptr->ensure_asset(r->spec->identity, platform, arch, hash_prefix);
}

// USER-MANAGED PACKAGE PATH: Double-check lock pattern
void run_check_phase_user_managed(recipe *r, engine &eng, sol::state_view lua) {
  // First check (pre-lock): See if work is needed
  tui::debug("phase check: running user check (pre-lock)");
  bool const check_passed_prelock{ run_check_verb(r, eng, lua) };
  tui::debug("phase check: user check returned %s",
             check_passed_prelock ? "true" : "false");

  if (check_passed_prelock) {
    // Check passed - no work needed, skip all phases
    tui::debug("phase check: check passed (pre-lock), skipping all phases");
    return;
  }

  // Check failed - work might be needed, acquire lock
  tui::debug(
      "phase check: check failed (pre-lock), acquiring lock for user-managed package");

  auto cache_result{ compute_hash_and_lookup_cache(r, lua) };

  if (cache_result.lock) {
    // Got lock - mark as user-managed for proper cleanup
    cache_result.lock->mark_user_managed();
    tui::debug("phase check: lock acquired, marked as user-managed");

    // Second check (post-lock): Detect races where another process completed work
    tui::debug("phase check: re-running user check (post-lock)");
    bool const check_passed_postlock{ run_check_verb(r, eng, lua) };
    tui::debug("phase check: re-check returned %s",
               check_passed_postlock ? "true" : "false");

    if (check_passed_postlock) {
      // Race detected: another process completed work while we waited for lock
      tui::debug(
          "phase check: re-check passed, releasing lock (another process completed)");
      // Lock destructor will run, purging entry_dir because user_managed flag is set.
      // This is correct: user-managed packages don't leave cache artifacts, so even
      // if install phase ran, there's nothing to preserve.
      return;
    }

    // Still needed - keep lock, phases will execute
    r->lock = std::move(cache_result.lock);
    tui::debug("phase check: re-check failed, keeping lock, phases will execute");
  } else {
    // Cache hit for user-managed package indicates inconsistent state:
    // check verb returned false (work needed) but cache entry exists.
    // This may indicate a buggy/non-deterministic check verb or race condition.
    r->asset_path = cache_result.asset_path;
    tui::warn(
        "phase check: unexpected cache hit for user-managed package at %s - "
        "check verb may be buggy or non-deterministic",
        cache_result.asset_path.string().c_str());
  }
}

// CACHE-MANAGED PACKAGE PATH: Traditional hash-based caching
void run_check_phase_cache_managed(recipe *r) {
  std::string const key{ r->spec->format_key() };
  sol::state_view lua{ *r->lua };

  auto cache_result{ compute_hash_and_lookup_cache(r, lua) };

  if (cache_result.lock) {  // Cache miss - acquire lock, subsequent phases will do work
    r->lock = std::move(cache_result.lock);
    tui::debug("phase check: [%s] CACHE MISS - pipeline will execute", key.c_str());
  } else {  // Cache hit - store asset_path, no lock means subsequent phases skip
    r->asset_path = cache_result.asset_path;
    tui::debug("phase check: [%s] CACHE HIT at %s - phases will skip",
               key.c_str(),
               cache_result.asset_path.string().c_str());
  }
}

void run_check_phase(recipe *r, engine &eng) {
  phase_trace_scope const phase_scope{ r->spec->identity,
                                       recipe_phase::asset_check,
                                       std::chrono::steady_clock::now() };

  sol::state_view lua{ *r->lua };

  // Check if recipe has check verb (user-managed package indicator)
  bool const has_check{ recipe_has_check_verb(r, lua) };

  if (has_check) {
    run_check_phase_user_managed(r, eng, lua);
  } else {
    run_check_phase_cache_managed(r);
  }
}

}  // namespace envy
