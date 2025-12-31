#include "phase_build.h"

#include "cache.h"
#include "engine.h"
#include "lua_ctx/lua_phase_context.h"
#include "lua_envy.h"
#include "lua_error_formatter.h"
#include "pkg.h"
#include "shell.h"
#include "trace.h"
#include "tui.h"
#include "util.h"

#include <chrono>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace envy {
namespace {

std::string format_build_error(std::string const &identity,
                               int exit_code,
                               std::optional<int> signal,
                               std::string const &stderr_capture) {
  std::string msg{ "[" + identity + "] Build failed" };

  if (signal) {
    msg += " (terminated by signal " + std::to_string(*signal) + ")";
  } else {
    msg += " (exit code " + std::to_string(exit_code) + ")";
  }

  if (!stderr_capture.empty()) {
    msg += "\n";
    // Include last portion of stderr for context
    constexpr size_t kMaxStderrBytes{ 2048 };
    if (stderr_capture.size() > kMaxStderrBytes) {
      msg += "... (truncated)\n";
      msg += stderr_capture.substr(stderr_capture.size() - kMaxStderrBytes);
    } else {
      msg += stderr_capture;
    }
    if (!msg.ends_with('\n')) { msg += '\n'; }
  }

  return msg;
}

// Common helper to execute a build script with proper output capture and error handling.
// Stdout is printed as it arrives; stderr is captured and included in error messages.
void execute_build_script(std::string_view script,
                          std::filesystem::path const &cwd,
                          std::string const &identity,
                          resolved_shell shell) {
  std::string stderr_capture;

  shell_env_t env{ shell_getenv() };
  shell_run_cfg const cfg{ .on_stdout_line =
                               [](std::string_view line) {
                                 tui::info("%.*s",
                                           static_cast<int>(line.size()),
                                           line.data());
                               },
                           .on_stderr_line =
                               [&](std::string_view line) {
                                 stderr_capture += line;
                                 stderr_capture += '\n';
                               },
                           .cwd = cwd,
                           .env = std::move(env),
                           .shell = shell };

  shell_result const result{ shell_run(script, cfg) };
  if (result.exit_code != 0) {
    auto const err{
      format_build_error(identity, result.exit_code, result.signal, stderr_capture)
    };
    tui::error("%s", err.c_str());
    throw std::runtime_error("Build failed for " + identity);
  }
}

void run_programmatic_build(sol::protected_function build_func,
                            std::filesystem::path const &fetch_dir,
                            std::filesystem::path const &stage_dir,
                            std::filesystem::path const &tmp_dir,
                            std::string const &identity,
                            engine &eng,
                            pkg *p) {
  tui::debug("phase build: running programmatic build function");

  // Set up Lua registry context for envy.* functions (run_dir = stage_dir)
  phase_context_guard ctx_guard{ &eng, p, stage_dir };

  sol::state_view lua{ build_func.lua_state() };
  sol::object opts{ lua.registry()[ENVY_OPTIONS_RIDX] };

  sol::protected_function_result build_result{ call_lua_function_with_enriched_errors(
      p,
      "BUILD",
      [&]() {
        return build_func(util_path_with_separator(stage_dir),
                          util_path_with_separator(fetch_dir),
                          util_path_with_separator(tmp_dir),
                          opts);
      }) };

  // If function returned a string, execute it via shell
  if (build_result.return_count() > 0) {
    sol::object return_value = build_result;
    if (return_value.is<std::string>()) {
      std::string const script{ return_value.as<std::string>() };
      tui::debug("phase build: function returned string, executing via shell");
      execute_build_script(script,
                           stage_dir,
                           identity,
                           shell_resolve_default(p->default_shell_ptr));
    }
  }
}

void run_shell_build(std::string_view script,
                     std::filesystem::path const &stage_dir,
                     std::string const &identity,
                     pkg *p) {
  tui::debug("phase build: running shell script");
  execute_build_script(script,
                       stage_dir,
                       identity,
                       shell_resolve_default(p->default_shell_ptr));
}

}  // namespace

void run_build_phase(pkg *p, engine &eng) {
  phase_trace_scope const phase_scope{ p->cfg->identity,
                                       pkg_phase::pkg_build,
                                       std::chrono::steady_clock::now() };
  if (!p->lock) {
    tui::debug("phase build: no lock (cache hit), skipping");
    return;
  }

  sol::state_view lua_view{ *p->lua };
  sol::object build_obj{ lua_view["BUILD"] };

  if (!build_obj.valid()) {
    tui::debug("phase build: no build field, skipping");
  } else if (build_obj.is<std::string>()) {
    std::string const script{ build_obj.as<std::string>() };
    run_shell_build(script, p->lock->stage_dir(), p->cfg->identity, p);
  } else if (build_obj.is<sol::protected_function>()) {
    run_programmatic_build(build_obj.as<sol::protected_function>(),
                           p->lock->fetch_dir(),
                           p->lock->stage_dir(),
                           p->lock->tmp_dir(),
                           p->cfg->identity,
                           eng,
                           p);
  } else {
    throw std::runtime_error("BUILD field must be nil, string, or function for " +
                             p->cfg->identity);
  }
}

}  // namespace envy
