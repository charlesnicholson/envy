#include "phase_install.h"

#include "cache.h"
#include "engine.h"
#include "lua_ctx/lua_phase_context.h"
#include "lua_envy.h"
#include "lua_error_formatter.h"
#include "phase_check.h"
#include "pkg.h"
#include "pkg_cfg.h"
#include "shell.h"
#include "trace.h"
#include "tui.h"
#include "tui_actions.h"
#include "util.h"

#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string_view>

namespace envy {
namespace {

bool directory_has_entries(std::filesystem::path const &dir) {
  std::error_code ec;
  if (!std::filesystem::exists(dir, ec) || ec) { return false; }

  std::filesystem::directory_iterator it{ dir, ec };
  if (ec) {
    throw std::runtime_error("Failed to enumerate directory " + dir.string() + ": " +
                             ec.message());
  }

  std::filesystem::directory_iterator end_iter;
  for (; it != end_iter; ++it) { return true; }

  return false;
}

bool run_shell_install(std::string_view script,
                       std::filesystem::path const &install_dir,
                       cache::scoped_entry_lock *lock,
                       std::string const &identity,
                       resolved_shell shell,
                       tui::section_handle tui_section,
                       std::filesystem::path const &cache_root) {
  tui::debug("phase install: running shell script");

  // Create run_progress tracker if TUI section is valid
  std::optional<tui_actions::run_progress> progress;
  if (tui_section) {
    progress.emplace(tui_section, identity, cache_root);
    progress->on_command_start(script);
  }

  shell_env_t env{ shell_getenv() };
  shell_run_cfg const cfg{ .on_output_line =
                               [&](std::string_view line) {
                                 if (progress) {
                                   progress->on_output_line(line);
                                 } else {
                                   tui::info("%.*s",
                                             static_cast<int>(line.size()),
                                             line.data());
                                 }
                               },
                           .cwd = install_dir,
                           .env = std::move(env),
                           .shell = shell };

  shell_result const result{ shell_run(script, cfg) };

  if (result.exit_code != 0) {
    if (result.signal) {
      throw std::runtime_error("Install shell script terminated by signal " +
                               std::to_string(*result.signal) + " for " + identity);
    }
    throw std::runtime_error("Install shell script failed for " + identity +
                             " (exit code " + std::to_string(result.exit_code) + ")");
  }

  if (lock) {
    lock->mark_install_complete();
    return true;
  }

  return false;
}

bool run_programmatic_install(sol::protected_function install_func,
                              cache::scoped_entry_lock *lock,
                              std::filesystem::path const &fetch_dir,
                              std::filesystem::path const &stage_dir,
                              std::filesystem::path const &install_dir,
                              std::filesystem::path const &tmp_dir,
                              std::string const &identity,
                              engine &eng,
                              pkg *p,
                              bool is_user_managed) {
  tui::debug("phase install: running programmatic install function");

  // Determine run_dir: install_dir for cache-managed, project_root for user-managed
  std::filesystem::path const run_dir{ is_user_managed
                                           ? pkg_cfg::compute_project_root(p->cfg)
                                           : install_dir };

  // Set up Lua registry context for envy.* functions
  phase_context_guard ctx_guard{ &eng, p, run_dir };

  sol::state_view lua{ install_func.lua_state() };
  sol::object opts{ lua.registry()[ENVY_OPTIONS_RIDX] };

  // no install_dir for user-managed packages.
  sol::object install_dir_arg{
    is_user_managed
        ? sol::object{ sol::lua_nil }
        : sol::object{ lua, sol::in_place, util_path_with_separator(install_dir) }
  };

  sol::object result_obj{ call_lua_function_with_enriched_errors(p, "INSTALL", [&]() {
    return install_func(install_dir_arg,
                        util_path_with_separator(stage_dir),
                        util_path_with_separator(fetch_dir),
                        util_path_with_separator(tmp_dir),
                        opts);
  }) };

  // Validate return type: must be nil or string
  sol::type const result_type{ result_obj.get_type() };
  if (result_type != sol::type::none && result_type != sol::type::lua_nil) {
    if (!result_obj.is<std::string>()) {
      throw std::runtime_error("install function for " + identity +
                               " must return nil or string, got " +
                               sol::type_name(lua.lua_state(), result_type));
    }

    // Returned string: spawn fresh shell with manifest defaults
    std::string const returned_script{ result_obj.as<std::string>() };
    tui::debug("phase install: running returned string from install function");

    // User-managed packages use project_root as cwd, cache-managed use install_dir
    std::filesystem::path const string_cwd{ is_user_managed
                                                ? pkg_cfg::compute_project_root(p->cfg)
                                                : install_dir };

    // Pass nullptr as lock for user-managed packages to skip mark_install_complete()
    // Cache-managed packages mark on shell exit 0
    return run_shell_install(returned_script,
                             string_cwd,
                             is_user_managed ? nullptr : lock,
                             identity,
                             shell_resolve_default(p->default_shell_ptr),
                             p->tui_section,
                             eng.cache_root());
  }

  // Function returned nil/none successfully - mark complete for cache-managed
  if (!is_user_managed && lock) {
    lock->mark_install_complete();
    return true;
  }

  return false;
}

bool promote_stage_to_install(cache::scoped_entry_lock *lock) {
  auto const install_dir{ lock->install_dir() };
  auto const stage_dir{ lock->stage_dir() };

  if (directory_has_entries(install_dir)) {
    tui::debug("phase install: install_dir already populated, marking complete");
    lock->mark_install_complete();
    return true;
  }

  if (directory_has_entries(stage_dir)) {
    tui::debug("phase install: promoting stage_dir contents to install_dir");
    std::filesystem::remove_all(install_dir);
    std::filesystem::create_directories(install_dir.parent_path());
    std::filesystem::rename(stage_dir, install_dir);
    lock->mark_install_complete();
    return true;
  }

  tui::debug("phase install: no outputs detected, leaving entry unmarked");
  return false;
}

}  // namespace

void run_install_phase(pkg *p, engine &eng) {
  phase_trace_scope const phase_scope{ p->cfg->identity,
                                       pkg_phase::pkg_install,
                                       std::chrono::steady_clock::now() };

  if (!p->lock) {  // Cache hit - no work to do
    tui::debug("phase install: no lock (cache hit), skipping");
    return;
  }

  cache::scoped_entry_lock::ptr_t lock{ std::move(p->lock) };
  std::filesystem::path const final_pkg_path{ lock->install_dir() };

  sol::state_view lua_view{ *p->lua };
  sol::object install_obj{ lua_view["INSTALL"] };
  bool marked_complete{ false };

  bool const is_user_managed{ pkg_has_check_verb(p, lua_view) };

  if (!install_obj.valid()) {
    marked_complete = promote_stage_to_install(lock.get());
  } else if (install_obj.is<std::string>()) {
    // String installs: run command, mark complete only if cache-managed
    // User-managed packages use manifest dir as cwd, cache-managed use install_dir
    std::filesystem::path const string_cwd{ is_user_managed
                                                ? pkg_cfg::compute_project_root(p->cfg)
                                                : lock->install_dir() };
    std::string script{ install_obj.as<std::string>() };
    marked_complete = run_shell_install(script,
                                        string_cwd,
                                        is_user_managed ? nullptr : lock.get(),
                                        p->cfg->identity,
                                        shell_resolve_default(p->default_shell_ptr),
                                        p->tui_section,
                                        eng.cache_root());
  } else if (install_obj.is<sol::protected_function>()) {
    marked_complete = run_programmatic_install(install_obj.as<sol::protected_function>(),
                                               lock.get(),
                                               lock->fetch_dir(),
                                               lock->stage_dir(),
                                               lock->install_dir(),
                                               lock->tmp_dir(),
                                               p->cfg->identity,
                                               eng,
                                               p,
                                               is_user_managed);
  } else {
    throw std::runtime_error("INSTALL field must be nil, string, or function for " +
                             p->cfg->identity);
  }

  // Cache-managed packages are auto-marked complete on successful INSTALL return
  // User-managed packages are never marked complete (ephemeral workspace)

  if (marked_complete) { p->pkg_path = final_pkg_path; }
}

}  // namespace envy
