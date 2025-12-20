#include "phase_build.h"

#include "cache.h"
#include "engine.h"
#include "lua_ctx/lua_phase_context.h"
#include "lua_envy.h"
#include "lua_error_formatter.h"
#include "recipe.h"
#include "shell.h"
#include "trace.h"
#include "tui.h"

#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

namespace envy {
namespace {

void run_programmatic_build(sol::protected_function build_func,
                            std::filesystem::path const &fetch_dir,
                            std::filesystem::path const &stage_dir,
                            std::filesystem::path const &tmp_dir,
                            std::string const &identity,
                            engine &eng,
                            recipe *r) {
  tui::debug("phase build: running programmatic build function");

  // Set up Lua registry context for envy.* functions (run_dir = stage_dir)
  phase_context_guard ctx_guard{ &eng, r, stage_dir };

  sol::state_view lua{ build_func.lua_state() };
  sol::object opts{ lua.registry()[ENVY_OPTIONS_RIDX] };

  sol::protected_function_result build_result{ call_lua_function_with_enriched_errors(
      r,
      "BUILD",
      [&]() {
        return build_func(stage_dir.string(), fetch_dir.string(), tmp_dir.string(), opts);
      }) };

  // If function returned a string, execute it via envy.run()
  if (build_result.return_count() > 0) {
    sol::object return_value = build_result;
    if (return_value.is<std::string>()) {
      std::string const script{ return_value.as<std::string>() };
      tui::debug("phase build: function returned string, executing via shell");

      shell_env_t env{ shell_getenv() };
      shell_run_cfg const cfg{ .on_output_line =
                                   [&](std::string_view line) {
                                     tui::info("%.*s",
                                               static_cast<int>(line.size()),
                                               line.data());
                                   },
                               .cwd = stage_dir,
                               .env = std::move(env),
                               .shell = shell_resolve_default(r->default_shell_ptr) };

      shell_result const result{ shell_run(script, cfg) };
      if (result.exit_code != 0) {
        throw std::runtime_error("Build function returned script failed for " + identity +
                                 " (exit code " + std::to_string(result.exit_code) + ")");
      }
    }
  }
}

void run_shell_build(std::string_view script,
                     std::filesystem::path const &stage_dir,
                     std::string const &identity,
                     recipe *r) {
  tui::debug("phase build: running shell script");

  shell_env_t env{ shell_getenv() };
  shell_run_cfg const cfg{ .on_output_line =
                               [&](std::string_view line) {
                                 tui::info("%.*s",
                                           static_cast<int>(line.size()),
                                           line.data());
                               },
                           .cwd = stage_dir,
                           .env = std::move(env),
                           .shell = shell_resolve_default(r->default_shell_ptr) };

  shell_result const result{ shell_run(script, cfg) };
  if (result.exit_code != 0) {
    throw std::runtime_error("Build shell script failed for " + identity + " (exit code " +
                             std::to_string(result.exit_code) + ")");
  }
}

}  // namespace

void run_build_phase(recipe *r, engine &eng) {
  phase_trace_scope const phase_scope{ r->spec->identity,
                                       recipe_phase::asset_build,
                                       std::chrono::steady_clock::now() };
  if (!r->lock) {
    tui::debug("phase build: no lock (cache hit), skipping");
    return;
  }

  sol::state_view lua_view{ *r->lua };
  sol::object build_obj{ lua_view["BUILD"] };

  if (!build_obj.valid()) {
    tui::debug("phase build: no build field, skipping");
  } else if (build_obj.is<std::string>()) {
    std::string const script{ build_obj.as<std::string>() };
    run_shell_build(script, r->lock->stage_dir(), r->spec->identity, r);
  } else if (build_obj.is<sol::protected_function>()) {
    run_programmatic_build(build_obj.as<sol::protected_function>(),
                           r->lock->fetch_dir(),
                           r->lock->stage_dir(),
                           r->lock->tmp_dir(),
                           r->spec->identity,
                           eng,
                           r);
  } else {
    throw std::runtime_error("BUILD field must be nil, string, or function for " +
                             r->spec->identity);
  }
}

}  // namespace envy
