#include "lua_ctx_bindings.h"

#include "extract.h"
#include "engine.h"
#include "lua_shell.h"
#include "manifest.h"
#include "recipe.h"
#include "shell.h"
#include "tui.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

#include <filesystem>
#include <string>
#include <vector>

namespace envy {

// Check if target_identity is a declared dependency of current recipe
bool is_declared_dependency(recipe *r, std::string const &target_identity) {
  for (std::string const &dep_identity : r->declared_dependencies) {
    if (dep_identity == target_identity) { return true; }
  }
  return false;
}

// Lua C function: ctx.asset(identity) -> path
// Graph topology guarantees dependency completion before parent accesses it
int lua_ctx_asset(lua_State *lua) {
  auto const *ctx{ static_cast<lua_ctx_common *>(
      lua_touserdata(lua, lua_upvalueindex(1))) };
  if (!ctx || !ctx->recipe_) { return luaL_error(lua, "ctx.asset: missing context"); }

  // Arg 1: identity (required)
  if (!lua_isstring(lua, 1)) {
    return luaL_error(lua, "ctx.asset: first argument must be identity string");
  }
  char const *identity{ lua_tostring(lua, 1) };

  // Validate dependency declaration (only checks direct dependencies)
  if (!is_declared_dependency(ctx->recipe_, identity)) {
    return luaL_error(lua,
                      "ctx.asset: recipe '%s' does not declare dependency on '%s'",
                      ctx->recipe_->spec.identity.c_str(),
                      identity);
  }

  // Look up dependency in recipe's dependency map
  recipe const *dep{ [&]() -> recipe const * {
    auto it{ ctx->recipe_->dependencies.find(identity) };
    if (it == ctx->recipe_->dependencies.end()) {
      luaL_error(lua, "ctx.asset: dependency not found in map: %s", identity);
    }
    return it->second;
  }() };

  if (!dep) { return luaL_error(lua, "ctx.asset: null dependency pointer: %s", identity); }

  // No need to check completion - graph edges guarantee dependency completed
  // before parent phase can run (via needed_by topology)

  // Return asset_path
  std::string const path{ dep->asset_path.string() };
  lua_pushstring(lua, path.c_str());
  return 1;
}

namespace {

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

  // Tier 2: Get manifest default_shell (if present)
  if (ctx->manifest_) {
    default_shell_cfg_t const manifest_default{ ctx->manifest_->get_default_shell(ctx) };
    if (manifest_default) {  // Convert default_shell_value to shell_run_cfg variant
      std::visit(match{
                     [&shell](shell_choice const &choice) { shell = choice; },
                     [&shell](custom_shell const &custom) {
                       std::visit([&shell](auto &&custom_type) { shell = custom_type; },
                                  custom);
                     },
                 },
                 *manifest_default);
    }
  }

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

  try {
    std::string combined_output;
    std::vector<std::string> output_lines;

    shell_run_cfg inv{ .on_output_line =
                           [&](std::string_view line) {
                             tui::info("%s", std::string{ line }.c_str());
                             output_lines.emplace_back(line);
                           },
                       .cwd = cwd,
                       .env = std::move(env),
                       .shell = shell };

    shell_result const result{ shell_run(script_view, inv) };

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

// Lua C function: ctx.copy(src, dst)
int lua_ctx_copy(lua_State *lua) {
  auto const *ctx{ static_cast<lua_ctx_common *>(
      lua_touserdata(lua, lua_upvalueindex(1))) };
  if (!ctx) { return luaL_error(lua, "ctx.copy: missing context"); }

  // Arg 1: source path (required)
  if (!lua_isstring(lua, 1)) {
    return luaL_error(lua, "ctx.copy: first argument must be source path string");
  }
  char const *src_str{ lua_tostring(lua, 1) };

  // Arg 2: destination path (required)
  if (!lua_isstring(lua, 2)) {
    return luaL_error(lua, "ctx.copy: second argument must be destination path string");
  }
  char const *dst_str{ lua_tostring(lua, 2) };

  std::filesystem::path src{ src_str };
  std::filesystem::path dst{ dst_str };

  // Resolve relative paths against run_dir
  if (src.is_relative()) { src = ctx->run_dir / src; }
  if (dst.is_relative()) { dst = ctx->run_dir / dst; }

  try {
    if (!std::filesystem::exists(src)) {
      return luaL_error(lua, "ctx.copy: source not found: %s", src_str);
    }

    // Auto-detect file vs directory
    if (std::filesystem::is_directory(src)) {
      std::filesystem::copy(src,
                            dst,
                            std::filesystem::copy_options::recursive |
                                std::filesystem::copy_options::overwrite_existing);
    } else {
      // Ensure parent directory exists for file copy
      if (dst.has_parent_path()) {
        std::filesystem::create_directories(dst.parent_path());
      }
      std::filesystem::copy_file(src,
                                 dst,
                                 std::filesystem::copy_options::overwrite_existing);
    }

    return 0;  // No return values
  } catch (std::exception const &e) { return luaL_error(lua, "ctx.copy: %s", e.what()); }
}

// Lua C function: ctx.move(src, dst)
int lua_ctx_move(lua_State *lua) {
  auto const *ctx{ static_cast<lua_ctx_common *>(
      lua_touserdata(lua, lua_upvalueindex(1))) };
  if (!ctx) { return luaL_error(lua, "ctx.move: missing context"); }

  // Arg 1: source path (required)
  if (!lua_isstring(lua, 1)) {
    return luaL_error(lua, "ctx.move: first argument must be source path string");
  }
  char const *src_str{ lua_tostring(lua, 1) };

  // Arg 2: destination path (required)
  if (!lua_isstring(lua, 2)) {
    return luaL_error(lua, "ctx.move: second argument must be destination path string");
  }
  char const *dst_str{ lua_tostring(lua, 2) };

  std::filesystem::path src{ src_str };
  std::filesystem::path dst{ dst_str };

  // Resolve relative paths against run_dir
  if (src.is_relative()) { src = ctx->run_dir / src; }
  if (dst.is_relative()) { dst = ctx->run_dir / dst; }

  try {
    if (!std::filesystem::exists(src)) {
      return luaL_error(lua, "ctx.move: source not found: %s", src_str);
    }

    if (dst.has_parent_path()) { std::filesystem::create_directories(dst.parent_path()); }

    // Error if destination already exists (don't delete anything automatically)
    if (std::filesystem::exists(dst)) {
      return luaL_error(lua,
                        "ctx.move: destination already exists: %s "
                        "(remove it explicitly first if you want to replace it)",
                        dst_str);
    }

    // Use rename (fast) when possible, falls back to copy+delete across filesystems
    std::filesystem::rename(src, dst);

    return 0;  // No return values
  } catch (std::exception const &e) { return luaL_error(lua, "ctx.move: %s", e.what()); }
}

// Lua C function: ctx.extract(filename, opts?)
int lua_ctx_extract(lua_State *lua) {
  auto const *ctx{ static_cast<lua_ctx_common *>(
      lua_touserdata(lua, lua_upvalueindex(1))) };
  if (!ctx) { return luaL_error(lua, "ctx.extract: missing context"); }

  // Arg 1: filename (required)
  if (!lua_isstring(lua, 1)) {
    return luaL_error(lua, "ctx.extract: first argument must be filename string");
  }
  char const *filename{ lua_tostring(lua, 1) };

  // Arg 2: options (optional)
  int strip_components{ 0 };
  if (lua_gettop(lua) >= 2 && lua_istable(lua, 2)) {
    lua_getfield(lua, 2, "strip");
    if (lua_isnumber(lua, -1)) {
      strip_components = static_cast<int>(lua_tointeger(lua, -1));
      if (strip_components < 0) {
        return luaL_error(lua, "ctx.extract: strip must be non-negative");
      }
    } else if (!lua_isnil(lua, -1)) {
      return luaL_error(lua, "ctx.extract: strip must be a number");
    }
    lua_pop(lua, 1);
  }

  std::filesystem::path const archive_path{ ctx->fetch_dir / filename };

  if (!std::filesystem::exists(archive_path)) {
    return luaL_error(lua, "ctx.extract: file not found: %s", filename);
  }

  try {
    std::uint64_t const files{
      extract(archive_path, ctx->run_dir, { .strip_components = strip_components })
    };
    lua_pushinteger(lua, static_cast<lua_Integer>(files));
    return 1;  // Return file count
  } catch (std::exception const &e) {
    return luaL_error(lua, "ctx.extract: %s", e.what());
  }
}

// Lua C function: ctx.ls(path)
// List directory contents for debugging, prints to TUI
int lua_ctx_ls(lua_State *lua) {
  if (auto const *ctx{
          static_cast<lua_ctx_common *>(lua_touserdata(lua, lua_upvalueindex(1))) };
      !ctx) {
    return luaL_error(lua, "ctx.ls: missing context");
  }

  if (lua_gettop(lua) < 1 || !lua_isstring(lua, 1)) {
    return luaL_error(lua, "ctx.ls: requires path argument");
  }

  std::filesystem::path const path{ lua_tostring(lua, 1) };

  tui::info("ctx.ls: %s", path.string().c_str());

  std::error_code ec;
  if (!std::filesystem::exists(path, ec)) {
    tui::info("  (directory does not exist or is inaccessible)");
    return 0;
  }

  if (!std::filesystem::is_directory(path, ec)) {
    tui::info("  (not a directory)");
    return 0;
  }

  for (auto const &entry : std::filesystem::directory_iterator(path, ec)) {
    std::string const type{ entry.is_directory() ? "d" : "f" };
    tui::info("  [%s] %s", type.c_str(), entry.path().filename().string().c_str());
  }

  if (ec) { tui::info("  (error reading directory: %s)", ec.message().c_str()); }

  return 0;  // No return value
}

}  // namespace

// Registration functions

void lua_ctx_bindings_register_run(lua_State *lua, void *context) {
  lua_pushlightuserdata(lua, context);
  lua_pushcclosure(lua, lua_ctx_run, 1);
  lua_setfield(lua, -2, "run");
}

void lua_ctx_bindings_register_asset(lua_State *lua, void *context) {
  lua_pushlightuserdata(lua, context);
  lua_pushcclosure(lua, lua_ctx_asset, 1);
  lua_setfield(lua, -2, "asset");
}

void lua_ctx_bindings_register_copy(lua_State *lua, void *context) {
  lua_pushlightuserdata(lua, context);
  lua_pushcclosure(lua, lua_ctx_copy, 1);
  lua_setfield(lua, -2, "copy");
}

void lua_ctx_bindings_register_move(lua_State *lua, void *context) {
  lua_pushlightuserdata(lua, context);
  lua_pushcclosure(lua, lua_ctx_move, 1);
  lua_setfield(lua, -2, "move");
}

void lua_ctx_bindings_register_extract(lua_State *lua, void *context) {
  lua_pushlightuserdata(lua, context);
  lua_pushcclosure(lua, lua_ctx_extract, 1);
  lua_setfield(lua, -2, "extract");
}

void lua_ctx_bindings_register_ls(lua_State *lua, void *context) {
  lua_pushlightuserdata(lua, context);
  lua_pushcclosure(lua, lua_ctx_ls, 1);
  lua_setfield(lua, -2, "ls");
}

}  // namespace envy
