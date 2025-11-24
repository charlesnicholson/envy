#include "lua_ctx_bindings.h"

#include "lua_shell.h"
#include "recipe.h"
#include "shell.h"
#include "trace.h"
#include "tui.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

#include <chrono>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace envy {

// Lua C function: ctx.run(script, opts?) -> {stdout, stderr}
int lua_ctx_run(lua_State *lua) {
  auto const *ctx{ static_cast<lua_ctx_common *>(
      lua_touserdata(lua, lua_upvalueindex(1))) };
  if (!ctx) { return luaL_error(lua, "ctx.run: missing context"); }

  // Arg 1: script (required)
  if (!lua_isstring(lua, 1)) {
    return luaL_error(lua, "ctx.run: first argument must be a string (shell script)");
  }

  size_t script_len{ 0 };
  char const *script{ lua_tolstring(lua, 1, &script_len) };
  std::string_view script_view{ script, script_len };

  // Arg 2: options (optional)
  std::optional<std::filesystem::path> cwd;
  shell_env_t env{ shell_getenv() };

  // resolution: call-site override -> manifest default -> platform default
  std::variant<shell_choice, custom_shell_file, custom_shell_inline> shell{
#if defined(_WIN32)
    shell_choice::powershell
#else
    shell_choice::bash
#endif
  };

  if (lua_gettop(lua) >= 2) {
    if (!lua_istable(lua, 2)) {
      return luaL_error(lua, "ctx.run: second argument must be a table (options)");
    }

    // Parse cwd option
    lua_getfield(lua, 2, "cwd");
    if (lua_isstring(lua, -1)) {
      char const *cwd_str{ lua_tostring(lua, -1) };
      std::filesystem::path cwd_path{ cwd_str };

      // If relative, make it relative to run_dir
      if (cwd_path.is_relative()) {
        cwd = ctx->run_dir / cwd_path;
      } else {
        cwd = cwd_path;
      }
    } else if (!lua_isnil(lua, -1)) {
      return luaL_error(lua, "ctx.run: cwd option must be a string");
    }
    lua_pop(lua, 1);

    // Parse env option (merge with inherited environment)
    lua_getfield(lua, 2, "env");
    if (lua_istable(lua, -1)) {
      lua_pushnil(lua);
      while (lua_next(lua, -2) != 0) {
        // Key at -2, value at -1
        if (lua_type(lua, -2) == LUA_TSTRING && lua_type(lua, -1) == LUA_TSTRING) {
          char const *key{ lua_tostring(lua, -2) };
          char const *value{ lua_tostring(lua, -1) };
          env[key] = value;
        }
        lua_pop(lua, 1);  // Pop value, keep key
      }
    } else if (!lua_isnil(lua, -1)) {
      return luaL_error(lua, "ctx.run: env option must be a table");
    }
    lua_pop(lua, 1);

    // Tier 3: Parse shell option (call-site override)
    lua_getfield(lua, 2, "shell");
    if (!lua_isnil(lua, -1)) {
      try {
        shell = parse_shell_config_from_lua(lua, -1, "ctx.run");
      } catch (std::exception const &e) {
        lua_pop(lua, 1);
        return luaL_error(lua, "%s", e.what());
      }
    }
    lua_pop(lua, 1);
  }

  if (!cwd) { cwd = ctx->run_dir; }  // Use run_dir as default cwd

  auto const start_time{ std::chrono::steady_clock::now() };

  if (tui::trace_enabled()) {
    std::string sanitized_script{ script_view };
    if (sanitized_script.size() > 100) {
      sanitized_script = sanitized_script.substr(0, 97) + "...";
    }
    ENVY_TRACE_LUA_CTX_RUN_START(ctx->recipe_->spec.identity,
                                 sanitized_script,
                                 cwd->string());
  }

  try {
    std::string combined_output;
    std::vector<std::string> output_lines;

    // Select output callback based on shell type (determined once, not per-line)
#if defined(_WIN32)
    bool const use_powershell_parsing{ std::holds_alternative<shell_choice>(shell) &&
                                       std::get<shell_choice>(shell) ==
                                           shell_choice::powershell };
#else
    bool constexpr use_powershell_parsing{ false };
#endif

    std::function<void(std::string_view)> on_line;

    if (use_powershell_parsing) {
      on_line = [&](std::string_view line) {  // Parse C0 chars (0x1C-0x1F) and route
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
        output_lines.emplace_back(msg);
      };
    } else {
      on_line = [&](std::string_view line) {  // All other shells: Route to info
        std::string const msg{ line };
        tui::info("%s", msg.c_str());
        output_lines.emplace_back(msg);
      };
    }

    shell_run_cfg const inv{ .on_output_line = std::move(on_line),
                             .cwd = cwd,
                             .env = std::move(env),
                             .shell = shell };

    shell_result const result{ shell_run(script_view, inv) };

    auto const duration_ms{ std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - start_time)
                                .count() };

    ENVY_TRACE_LUA_CTX_RUN_COMPLETE(ctx->recipe_->spec.identity,
                                     result.exit_code,
                                     static_cast<std::int64_t>(duration_ms));

    if (result.exit_code != 0) {
      if (result.signal) {
        return luaL_error(lua,
                          "ctx.run: shell script terminated by signal %d for %s",
                          *result.signal,
                          ctx->recipe_->spec.identity.c_str());
      } else {
        return luaL_error(lua,
                          "ctx.run: shell script failed with exit code %d for %s",
                          result.exit_code,
                          ctx->recipe_->spec.identity.c_str());
      }
    }

    // Build combined output for return (stdout and stderr currently merged)
    for (auto const &line : output_lines) {
      combined_output += line;
      combined_output += '\n';
    }

    // Return {stdout, stderr} table (stderr empty until shell_run separates streams)
    lua_createtable(lua, 0, 2);
    lua_pushstring(lua, combined_output.c_str());
    lua_setfield(lua, -2, "stdout");
    lua_pushstring(lua, "");
    lua_setfield(lua, -2, "stderr");

    return 1;  // Return table
  } catch (std::exception const &e) { return luaL_error(lua, "ctx.run: %s", e.what()); }
}

}  // namespace envy
