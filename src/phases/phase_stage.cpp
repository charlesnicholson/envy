#include "phase_stage.h"

#include "cache.h"
#include "engine.h"
#include "extract.h"
#include "lua_ctx/lua_ctx_bindings.h"
#include "lua_ctx/lua_phase_context.h"
#include "lua_envy.h"
#include "lua_error_formatter.h"
#include "recipe.h"
#include "shell.h"
#include "sol_util.h"
#include "trace.h"
#include "tui.h"
#include "tui_actions.h"

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

struct stage_phase_ctx : lua_ctx_common {
  // run_dir inherited from base is dest_dir (stage_dir)
};

sol::table build_stage_phase_ctx_table(sol::state_view lua,
                                       std::string const &identity,
                                       stage_phase_ctx *ctx) {
  sol::table ctx_table{ lua.create_table() };
  ctx_table["identity"] = identity;
  ctx_table["fetch_dir"] = ctx->fetch_dir.string();
  ctx_table["stage_dir"] = ctx->run_dir.string();
  lua_ctx_add_common_bindings(ctx_table, ctx);
  return ctx_table;
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

void run_extract_stage(extract_totals const &totals,
                       std::filesystem::path const &fetch_dir,
                       std::filesystem::path const &dest_dir,
                       std::string const &identity,
                       tui::section_handle section,
                       int strip_components) {
  tui::debug("phase stage: extracting (strip=%d)", strip_components);

  std::vector<std::string> items;
  for (auto const &entry : std::filesystem::directory_iterator(fetch_dir)) {
    if (!entry.is_regular_file()) { continue; }
    if (entry.path().filename() == "envy-complete") { continue; }
    items.push_back(entry.path().filename().string());
  }

  tui_actions::extract_all_progress_tracker tracker{ section, identity, items, totals };
  auto [progress_cb, on_file_cb] = tracker.make_callbacks();

  extract_all_archives(
      fetch_dir,
      dest_dir,
      strip_components,
      progress_cb,
      identity,
      items.size() > 1 ? on_file_cb : std::function<void(std::string const &)>{},
      totals);
}

void run_programmatic_stage(sol::protected_function stage_func,
                            std::filesystem::path const &fetch_dir,
                            std::filesystem::path const &stage_dir,
                            std::filesystem::path const &tmp_dir,
                            std::string const &identity,
                            engine &eng,
                            recipe *r) {
  tui::debug("phase stage: running imperative stage function");

  // Set up Lua registry context for envy.* functions
  phase_context_guard ctx_guard{ &eng, r };

  sol::state_view lua{ stage_func.lua_state() };
  sol::object opts{ lua.registry()[ENVY_OPTIONS_RIDX] };

  call_lua_function_with_enriched_errors(r, "STAGE", [&]() {
    return stage_func(fetch_dir.string(), stage_dir.string(), tmp_dir.string(), opts);
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

void run_stage_phase(recipe *r, engine &eng) {
  phase_trace_scope const phase_scope{ r->spec->identity,
                                       recipe_phase::asset_stage,
                                       std::chrono::steady_clock::now() };

  cache::scoped_entry_lock *lock{ r->lock.get() };
  if (!lock) {
    tui::debug("phase stage: no lock (cache hit), skipping");
    return;
  }

  std::string const &identity{ r->spec->identity };
  sol::state_view lua_view{ *r->lua };
  std::filesystem::path const stage_dir{ determine_stage_destination(lua_view, lock) };

  sol::object stage_obj{ lua_view["STAGE"] };

  if (!fetch_dir_has_files(lock->fetch_dir())) {
    tui::debug("phase stage: no files in fetch_dir, skipping");
    return;
  }

  if (!stage_obj.valid()) {
    extract_totals const totals{ compute_extract_totals(lock->fetch_dir()) };
    run_extract_stage(totals, lock->fetch_dir(), stage_dir, identity, r->tui_section, 0);
  } else if (stage_obj.is<std::string>()) {
    auto const script_str{ stage_obj.as<std::string>() };
    run_shell_stage(script_str,
                    stage_dir,
                    identity,
                    shell_resolve_default(r->default_shell_ptr));
  } else if (stage_obj.is<sol::protected_function>()) {
    run_programmatic_stage(stage_obj.as<sol::protected_function>(),
                           lock->fetch_dir(),
                           stage_dir,
                           lock->tmp_dir(),
                           identity,
                           eng,
                           r);
  } else if (stage_obj.is<sol::table>()) {
    stage_options const opts{ parse_stage_options(stage_obj.as<sol::table>(), identity) };
    extract_totals const totals{ compute_extract_totals(lock->fetch_dir()) };
    run_extract_stage(totals,
                      lock->fetch_dir(),
                      stage_dir,
                      identity,
                      r->tui_section,
                      opts.strip_components);
  } else {
    throw std::runtime_error("STAGE field must be nil, string, table, or function for " +
                             identity);
  }
}

}  // namespace envy
