#include "phase_stage.h"

#include "cache.h"
#include "engine.h"
#include "extract.h"
#include "lua_ctx/lua_phase_context.h"
#include "lua_envy.h"
#include "lua_error_formatter.h"
#include "pkg.h"
#include "shell.h"
#include "sol_util.h"
#include "trace.h"
#include "tui.h"
#include "util.h"

#include <chrono>
#include <filesystem>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace envy {
namespace {

bool fetch_dir_has_files(std::filesystem::path const &fetch_dir) {
  if (!std::filesystem::exists(fetch_dir)) { return false; }
  for (auto const &entry : std::filesystem::directory_iterator(fetch_dir)) {
    if (!entry.is_regular_file()) { continue; }
    if (entry.path().filename() == "envy-complete") { continue; }
    return true;
  }
  return false;
}

std::filesystem::path determine_stage_destination(sol::state_view lua,
                                                  cache::scoped_entry_lock const *lock) {
  sol::object stage_obj{ lua["STAGE"] };
  sol::object build_obj{ lua["BUILD"] };
  sol::object install_obj{ lua["INSTALL"] };

  bool const has_custom_phases{ stage_obj.is<sol::protected_function>() ||
                                build_obj.is<sol::protected_function>() ||
                                install_obj.is<sol::protected_function>() };

  std::filesystem::path const dest_dir{ has_custom_phases ? lock->stage_dir()
                                                          : lock->install_dir() };

  tui::debug("phase stage: destination=%s (custom_phases=%s)",
             dest_dir.string().c_str(),
             has_custom_phases ? "true" : "false");

  return dest_dir;
}

struct stage_options {
  int strip_components{ 0 };
};

stage_options parse_stage_options(sol::table const &stage_tbl, std::string const &key) {
  stage_options opts;

  if (auto strip{ sol_util_get_optional<int>(stage_tbl, "strip", key) }) {
    if (*strip < 0) {
      throw std::runtime_error("stage.strip must be non-negative for " + key);
    }
    opts.strip_components = *strip;
  }

  return opts;
}

void run_programmatic_stage(sol::protected_function stage_func,
                            std::filesystem::path const &fetch_dir,
                            std::filesystem::path const &stage_dir,
                            std::filesystem::path const &tmp_dir,
                            std::string const &identity,
                            engine &eng,
                            pkg *p) {
  tui::debug("phase stage: running imperative stage function");

  // Set up Lua registry context for envy.* functions (run_dir = stage_dir)
  phase_context_guard ctx_guard{ &eng, p, stage_dir };

  sol::state_view lua{ stage_func.lua_state() };
  sol::object opts{ lua.registry()[ENVY_OPTIONS_RIDX] };

  call_lua_function_with_enriched_errors(p, "STAGE", [&]() {
    return stage_func(util_path_with_separator(fetch_dir),
                      util_path_with_separator(stage_dir),
                      util_path_with_separator(tmp_dir),
                      opts);
  });
}

void run_shell_stage(std::string_view script,
                     std::filesystem::path const &dest_dir,
                     std::string const &identity,
                     resolved_shell shell) {
  tui::debug("phase stage: running shell script");

  shell_env_t env{ shell_getenv() };

  shell_run_cfg inv{ .on_output_line =
                         [&](std::string_view line) {
                           tui::info("%.*s", static_cast<int>(line.size()), line.data());
                         },
                     .cwd = dest_dir,
                     .env = std::move(env),
                     .shell = std::move(shell) };

  shell_result const result{ shell_run(script, inv) };

  if (result.exit_code != 0) {
    if (result.signal) {
      throw std::runtime_error("Stage shell script failed for " + identity +
                               " (terminated by signal " + std::to_string(*result.signal) +
                               ")");
    } else {
      throw std::runtime_error("Stage shell script failed for " + identity +
                               " (exit code " + std::to_string(result.exit_code) + ")");
    }
  }
}

}  // namespace

void run_stage_phase(pkg *p, engine &eng) {
  phase_trace_scope const phase_scope{ p->cfg->identity,
                                       pkg_phase::pkg_stage,
                                       std::chrono::steady_clock::now() };

  cache::scoped_entry_lock *lock{ p->lock.get() };
  if (!lock) {
    tui::debug("phase stage: no lock (cache hit), skipping");
    return;
  }

  std::string const &identity{ p->cfg->identity };
  sol::state_view lua_view{ *p->lua };
  std::filesystem::path const stage_dir{ determine_stage_destination(lua_view, lock) };

  sol::object stage_obj{ lua_view["STAGE"] };

  if (!fetch_dir_has_files(lock->fetch_dir())) {
    tui::debug("phase stage: no files in fetch_dir, skipping");
    return;
  }

  if (!stage_obj.valid()) {
    tui::debug("phase stage: extracting (strip=0)");
    extract_all_archives(lock->fetch_dir(), stage_dir, 0, identity, p->tui_section);
  } else if (stage_obj.is<std::string>()) {
    auto const script_str{ stage_obj.as<std::string>() };
    run_shell_stage(script_str,
                    stage_dir,
                    identity,
                    shell_resolve_default(p->default_shell_ptr));
  } else if (stage_obj.is<sol::protected_function>()) {
    run_programmatic_stage(stage_obj.as<sol::protected_function>(),
                           lock->fetch_dir(),
                           stage_dir,
                           lock->tmp_dir(),
                           identity,
                           eng,
                           p);
  } else if (stage_obj.is<sol::table>()) {
    stage_options const opts{ parse_stage_options(stage_obj.as<sol::table>(), identity) };
    tui::debug("phase stage: extracting (strip=%d)", opts.strip_components);
    extract_all_archives(lock->fetch_dir(),
                         stage_dir,
                         opts.strip_components,
                         identity,
                         p->tui_section);
  } else {
    throw std::runtime_error("STAGE field must be nil, string, table, or function for " +
                             identity);
  }
}

}  // namespace envy
