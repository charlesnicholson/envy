#include "phase_check.h"

#include "blake3_util.h"
#include "lua_ctx_bindings.h"
#include "manifest.h"
#include "shell.h"
#include "tui.h"
#include "util.h"

#include <stdexcept>
#include <utility>

namespace envy {

// Helper: Run string-based check verb (check = "command")
bool run_check_string(recipe *r, graph_state &state, std::string_view check_cmd) {
  tui::trace("phase check: executing string check: %s", std::string(check_cmd).c_str());

  // Build shell config using manifest's default_shell
  shell_run_cfg cfg;
  cfg.on_output_line = [](std::string_view line) {
    tui::trace("check output: %s", std::string(line).c_str());
  };
  cfg.env = shell_getenv();

  // Get manifest default_shell if present
  if (state.manifest_) {
    // Create minimal context for get_default_shell (only used if default_shell is a
    // function)
    lua_ctx_common ctx{ .fetch_dir = {},  // Empty - not used in check phase
                        .run_dir = {},    // Empty - not used in check phase
                        .state = &state,
                        .recipe_ = r,
                        .manifest_ = state.manifest_ };

    default_shell_cfg_t const manifest_default{ state.manifest_->get_default_shell(&ctx) };
    if (manifest_default) {
      std::visit(match{ [&cfg](shell_choice const &choice) { cfg.shell = choice; },
                        [&cfg](custom_shell const &custom) {
                          std::visit(
                              [&cfg](auto &&custom_type) { cfg.shell = custom_type; },
                              custom);
                        } },
                 *manifest_default);
    }
  }

  // Run the check command
  shell_result result;
  try {
    result = shell_run(check_cmd, cfg);
  } catch (std::exception const &e) {
    throw std::runtime_error("check command failed for " + r->spec.identity + ": " +
                             e.what());
  }

  bool const check_passed{ result.exit_code == 0 };
  tui::trace("phase check: string check exit_code=%d (check %s)",
             result.exit_code,
             check_passed ? "passed" : "failed");
  return check_passed;
}

// Helper: Run function-based check verb (check = function(ctx) ...)
bool run_check_function(recipe *r, lua_State *lua) {
  tui::trace("phase check: executing function check");

  lua_newtable(lua);  // Empty ctx table

  if (lua_pcall(lua, 1, 1, 0) != LUA_OK) {
    char const *err{ lua_tostring(lua, -1) };
    lua_pop(lua, 1);
    throw std::runtime_error("check() failed for " + r->spec.identity + ": " +
                             (err ? err : "unknown error"));
  }

  bool const check_passed{ static_cast<bool>(lua_toboolean(lua, -1)) };
  lua_pop(lua, 1);

  tui::trace("phase check: function check returned %s", check_passed ? "true" : "false");
  return check_passed;
}

// Helper: Run check verb (dispatches to string or function)
bool run_check_verb(recipe *r, graph_state &state, lua_State *lua) {
  lua_getglobal(lua, "check");

  if (lua_isfunction(lua, -1)) {
    return run_check_function(r, lua);
  } else if (lua_isstring(lua, -1)) {
    std::string_view const check_cmd{ lua_tostring(lua, -1) };
    lua_pop(lua, 1);
    return run_check_string(r, state, check_cmd);
  } else {
    lua_pop(lua, 1);
    return false;  // No check verb, return false (work needed)
  }
}

// Helper: Check if recipe has check verb
bool recipe_has_check_verb(recipe *r, lua_State *lua) {
  lua_getglobal(lua, "check");
  bool const has_check{ lua_isfunction(lua, -1) || lua_isstring(lua, -1) };
  lua_pop(lua, 1);
  return has_check;
}

void run_check_phase(recipe *r, graph_state &state) {
  std::string const key{ r->spec.format_key() };
  tui::trace("phase check START [%s]", key.c_str());
  trace_on_exit trace_end{ "phase check END [" + key + "]" };

  lua_State *lua = r->lua_state.get();
  if (!lua) { throw std::runtime_error("No lua_state for recipe: " + r->spec.identity); }

  // Check if recipe has check verb (user-managed package indicator)
  bool const has_check{ recipe_has_check_verb(r, lua) };

  if (has_check) {
    // USER-MANAGED PACKAGE PATH: Double-check lock pattern

    // First check (pre-lock): See if work is needed
    tui::trace("phase check: running user check (pre-lock)");
    bool const check_passed_prelock{ run_check_verb(r, state, lua) };
    tui::trace("phase check: user check returned %s",
               check_passed_prelock ? "true" : "false");

    if (check_passed_prelock) {
      // Check passed - no work needed, skip all phases
      tui::trace("phase check: check passed (pre-lock), skipping all phases");
      return;
    }

    // Check failed - work might be needed, acquire lock
    tui::trace(
        "phase check: check failed (pre-lock), acquiring lock for user-managed package");

    std::string const key_for_hash{ r->spec.format_key() };
    auto const digest{ blake3_hash(key_for_hash.data(), key_for_hash.size()) };
    r->canonical_identity_hash =
        util_bytes_to_hex(digest.data(), 32);  // Full hash: 64 hex chars
    std::string const hash_prefix{ util_bytes_to_hex(digest.data(),
                                                     8) };  // For cache path: 16 hex chars

    std::string const platform{ lua_global_to_string(lua, "ENVY_PLATFORM") };
    std::string const arch{ lua_global_to_string(lua, "ENVY_ARCH") };

    auto cache_result{
      state.cache_.ensure_asset(r->spec.identity, platform, arch, hash_prefix)
    };

    if (cache_result.lock) {
      // Got lock - mark as user-managed for proper cleanup
      cache_result.lock->mark_user_managed();
      tui::trace("phase check: lock acquired, marked as user-managed");

      // Second check (post-lock): Detect races where another process completed work
      tui::trace("phase check: re-running user check (post-lock)");
      bool const check_passed_postlock{ run_check_verb(r, state, lua) };
      tui::trace("phase check: re-check returned %s",
                 check_passed_postlock ? "true" : "false");

      if (check_passed_postlock) {
        // Race detected: another process completed work while we waited for lock
        tui::trace(
            "phase check: re-check passed, releasing lock (another process completed)");
        // Lock destructor will run, purging entry_dir, phases skip
        return;
      }

      // Still needed - keep lock, phases will execute
      r->lock = std::move(cache_result.lock);
      tui::trace("phase check: re-check failed, keeping lock, phases will execute");
    } else {
      // Cache hit (shouldn't happen for user-managed, but handle it)
      r->asset_path = cache_result.asset_path;
      tui::trace("phase check: cache hit for user-managed package at %s",
                 cache_result.asset_path.string().c_str());
    }

  } else {
    // CACHE-MANAGED PACKAGE PATH: Traditional hash-based caching

    std::string const key_for_hash{ r->spec.format_key() };

    auto const digest{ blake3_hash(key_for_hash.data(), key_for_hash.size()) };
    r->canonical_identity_hash =
        util_bytes_to_hex(digest.data(), 32);  // Full hash: 64 hex chars
    std::string const hash_prefix{ util_bytes_to_hex(digest.data(),
                                                     8) };  // For cache path: 16 hex chars

    std::string const platform{ lua_global_to_string(lua, "ENVY_PLATFORM") };
    std::string const arch{ lua_global_to_string(lua, "ENVY_ARCH") };

    auto cache_result{
      state.cache_.ensure_asset(r->spec.identity, platform, arch, hash_prefix)
    };

    if (cache_result.lock) {  // Cache miss - acquire lock, subsequent phases will do work
      r->lock = std::move(cache_result.lock);
      tui::trace("phase check: [%s] CACHE MISS - pipeline will execute", key.c_str());
    } else {  // Cache hit - store asset_path, no lock means subsequent phases skip
      r->asset_path = cache_result.asset_path;
      tui::trace("phase check: [%s] CACHE HIT at %s - phases will skip",
                 key.c_str(),
                 cache_result.asset_path.string().c_str());
    }
  }
}

}  // namespace envy
