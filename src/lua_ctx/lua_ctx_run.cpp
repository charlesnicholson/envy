#include "lua_ctx_bindings.h"

#include "lua_shell.h"
#include "recipe.h"
#include "shell.h"
#include "trace.h"
#include "tui.h"

#include <chrono>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace envy {

std::function<sol::table(sol::object, sol::optional<sol::object>, sol::this_state)>
make_ctx_run(lua_ctx_common *ctx) {
  return [ctx](sol::object script_obj,
               sol::optional<sol::object> opts_obj,
               sol::this_state L) -> sol::table {
    // Validate script argument
    if (!script_obj.is<std::string>()) {
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
    if (opts_table) {
      sol::table opts{ *opts_table };
      sol::optional<sol::object> quiet_obj = opts["quiet"];
      if (quiet_obj && quiet_obj->valid() && quiet_obj->get_type() != sol::type::lua_nil) {
        if (!quiet_obj->is<bool>()) {
          throw std::runtime_error("ctx.run: quiet must be a boolean");
        }
        quiet = quiet_obj->as<bool>();
      }

      sol::optional<sol::object> capture_obj = opts["capture"];
      if (capture_obj && capture_obj->valid() &&
          capture_obj->get_type() != sol::type::lua_nil) {
        if (!capture_obj->is<bool>()) {
          throw std::runtime_error("ctx.run: capture must be a boolean");
        }
        capture = capture_obj->as<bool>();
      }
    }

    std::string stdout_buffer;
    std::string stderr_buffer;

#if defined(_WIN32)
    bool const use_powershell_parsing{ std::holds_alternative<shell_choice>(shell) &&
                                       std::get<shell_choice>(shell) ==
                                           shell_choice::powershell };
#else
    bool constexpr use_powershell_parsing{ false };
#endif

    auto append_line{ [](std::string &buffer, std::string_view line) {
      buffer.append(line);
      buffer.push_back('\n');
    } };

    std::function<void(std::string_view)> on_stdout_capture;
    std::function<void(std::string_view)> on_stderr_capture;
    if (capture) {
      on_stdout_capture = [&](std::string_view line) { append_line(stdout_buffer, line); };
      on_stderr_capture = [&](std::string_view line) { append_line(stderr_buffer, line); };
    }

    std::function<void(std::string_view)> on_line;
    if (quiet) {
      on_line = [](std::string_view) {};
    }
#if defined(_WIN32)
    else if (use_powershell_parsing) {
      on_line = [&](std::string_view line) {
        std::string const msg{ !line.empty() && line[0] >= '\x1C' && line[0] <= '\x1F'
                                   ? line.substr(1)
                                   : line };
        if (line.starts_with('\x1C')) {
          tui::error("%s", msg.c_str());
        } else if (line.starts_with('\x1D')) {
          tui::warn("%s", msg.c_str());
        } else if (line.starts_with('\x1F')) {
          tui::debug("%s", msg.c_str());
        } else {
          tui::info("%s", msg.c_str());
        }
      };
    }
#endif
    else {
      on_line = [&](std::string_view line) {
        std::string const msg{ line };
        tui::info("%s", msg.c_str());
      };
    }

    shell_run_cfg const inv{ .on_output_line = std::move(on_line),
                             .on_stdout_line = std::move(on_stdout_capture),
                             .on_stderr_line = std::move(on_stderr_capture),
                             .cwd = cwd,
                             .env = std::move(env),
                             .shell = shell };

    shell_result const result{ shell_run(script_view, inv) };

    auto const duration_ms{ std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - start_time)
                                .count() };

    ENVY_TRACE_LUA_CTX_RUN_COMPLETE(ctx->recipe_->spec->identity,
                                    result.exit_code,
                                    static_cast<std::int64_t>(duration_ms));

    if (result.exit_code != 0) {
      if (result.signal) {
        throw std::runtime_error("ctx.run: shell script terminated by signal " +
                                 std::to_string(*result.signal) + " for " +
                                 ctx->recipe_->spec->identity);
      } else {
        throw std::runtime_error("ctx.run: shell script failed with exit code " +
                                 std::to_string(result.exit_code) + " for " +
                                 ctx->recipe_->spec->identity);
      }
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
