#include "lua_ctx_bindings.h"

#include "engine.h"
#include "lua_shell.h"
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
#include <string>
#include <string_view>

namespace envy {

namespace {

std::string format_run_error(std::string_view script,
                             int exit_code,
                             std::optional<int> signal,
                             std::string const &stdout_str,
                             std::string const &stderr_str,
                             std::string const &identity) {
  std::string error_msg;

  if (signal) {
    error_msg = "ctx.run: shell script terminated by signal " + std::to_string(*signal) +
                " for " + identity;
  } else {
    error_msg = "ctx.run: command failed with exit code " + std::to_string(exit_code) +
                " for " + identity;
  }

  error_msg += "\nCommand: ";
  error_msg += script;
  error_msg += "\n";

  if (!stdout_str.empty()) {
    error_msg += "\n--- stdout ---\n";
    error_msg += stdout_str;
    if (!stdout_str.ends_with('\n')) { error_msg += "\n"; }
  }

  if (!stderr_str.empty()) {
    error_msg += "\n--- stderr ---\n";
    error_msg += stderr_str;
    if (!stderr_str.ends_with('\n')) { error_msg += "\n"; }
  }

  return error_msg;
}

}  // namespace

std::function<sol::table(sol::object, sol::optional<sol::object>, sol::this_state)>
make_ctx_run(lua_ctx_common *ctx) {
  return [ctx](sol::object script_obj,
               sol::optional<sol::object> opts_obj,
               sol::this_state L) -> sol::table {
    if (!script_obj.is<std::string>()) {  // Validate script argument
      throw std::runtime_error("ctx.run: first argument must be a string (shell script)");
    }
    std::string const script{ script_obj.as<std::string>() };
    std::string_view script_view{ script };

    // Validate options argument if provided
    if (opts_obj && opts_obj->valid() && opts_obj->get_type() != sol::type::lua_nil &&
        !opts_obj->is<sol::table>()) {
      throw std::runtime_error("ctx.run: second argument must be a table (options)");
    }

    sol::optional<sol::table> opts_table;
    if (opts_obj && opts_obj->is<sol::table>()) {
      opts_table = opts_obj->as<sol::table>();
    }

    std::optional<std::filesystem::path> cwd;
    shell_env_t env{ shell_getenv() };

    resolved_shell shell{ shell_resolve_default(
        ctx && ctx->recipe_ ? ctx->recipe_->default_shell_ptr : nullptr) };

    if (opts_table) {
      sol::table opts{ *opts_table };

      sol::optional<std::string> cwd_str = opts["cwd"];
      if (cwd_str) {
        std::filesystem::path cwd_path{ *cwd_str };
        cwd = cwd_path.is_relative() ? ctx->run_dir / cwd_path : cwd_path;
      }

      sol::optional<sol::table> env_table = opts["env"];
      if (env_table) {
        for (auto const &[key, value] : *env_table) {
          if (key.is<std::string>() && value.is<std::string>()) {
            env[key.as<std::string>()] = value.as<std::string>();
          }
        }
      }

      sol::optional<sol::object> shell_obj = opts["shell"];
      if (shell_obj && shell_obj->valid()) {
        shell = parse_shell_config_from_lua(*shell_obj, "ctx.run");
      }
    }

    if (!cwd) { cwd = ctx->run_dir; }

    auto const start_time{ std::chrono::steady_clock::now() };

    if (tui::trace_enabled()) {
      std::string sanitized_script{ script_view };
      if (sanitized_script.size() > 100) {
        sanitized_script = sanitized_script.substr(0, 97) + "...";
      }
      ENVY_TRACE_LUA_CTX_RUN_START(ctx->recipe_->spec->identity,
                                   sanitized_script,
                                   cwd->string());
    }

    bool quiet{ false };
    bool capture{ false };
    bool check{ false };
    bool interactive{ false };
    if (opts_table) {
      sol::table opts{ *opts_table };
      quiet = sol_util_get_or_default<bool>(opts, "quiet", false, "ctx.run");
      capture = sol_util_get_or_default<bool>(opts, "capture", false, "ctx.run");
      check = sol_util_get_or_default<bool>(opts, "check", false, "ctx.run");
      interactive = sol_util_get_or_default<bool>(opts, "interactive", false, "ctx.run");
    }

    // Auto-manage TUI progress for ctx.run()
    std::optional<tui_actions::run_progress> progress;
    if (ctx && ctx->recipe_ && ctx->recipe_->tui_section && ctx->engine_) {
      progress.emplace(ctx->recipe_->tui_section,
                       ctx->recipe_->spec->identity,
                       ctx->engine_->cache_root());
      progress->on_command_start(script_view);
    }

    std::string stdout_buffer;
    std::string stderr_buffer;

    shell_run_cfg const inv{
      .on_output_line =
          [&](std::string_view line) {
            if (progress && !quiet) {
              progress->on_output_line(line);
            } else if (!quiet) {
              tui::info("%s", std::string{ line }.c_str());
            }
          },
      .on_stdout_line = [&](std::string_view line) { (stdout_buffer += line) += '\n'; },
      .on_stderr_line = [&](std::string_view line) { (stderr_buffer += line) += '\n'; },
      .cwd = cwd,
      .env = std::move(env),
      .shell = shell
    };

    std::optional<tui::interactive_mode_guard> guard;
    if (interactive) { guard.emplace(); }

    shell_result const result{ shell_run(script_view, inv) };

    auto const duration_ms{ std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - start_time)
                                .count() };

    ENVY_TRACE_LUA_CTX_RUN_COMPLETE(ctx->recipe_->spec->identity,
                                    result.exit_code,
                                    static_cast<std::int64_t>(duration_ms));

    if (result.signal) {
      auto const err{ format_run_error(script_view,
                                       result.exit_code,
                                       result.signal,
                                       stdout_buffer,
                                       stderr_buffer,
                                       ctx->recipe_->spec->identity) };
      tui::error("%s", err.c_str());
      throw std::runtime_error(err);
    }

    if (check && result.exit_code != 0) {
      auto const err{ format_run_error(script_view,
                                       result.exit_code,
                                       std::nullopt,
                                       stdout_buffer,
                                       stderr_buffer,
                                       ctx->recipe_->spec->identity) };
      tui::error("%s", err.c_str());
      throw std::runtime_error(err);
    }

    sol::state_view lua_view{ L };
    sol::table return_table{ lua_view.create_table() };
    return_table["exit_code"] = result.exit_code;
    if (capture) {
      return_table["stdout"] = stdout_buffer;
      return_table["stderr"] = stderr_buffer;
    } else {
      return_table["stdout"] = sol::lua_nil;
      return_table["stderr"] = sol::lua_nil;
    }

    return return_table;
  };
}

}  // namespace envy
