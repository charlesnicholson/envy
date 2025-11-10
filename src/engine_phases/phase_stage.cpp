#include "phase_stage.h"

#include "extract.h"
#include "lua_ctx_bindings.h"
#include "lua_util.h"
#include "shell.h"
#include "tui.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

#include <filesystem>
#include <stdexcept>
#include <string>
#include <tuple>

namespace envy {
namespace {

// Context data for Lua C functions (stored as userdata upvalue)
struct stage_context {
  std::filesystem::path fetch_dir;
  std::filesystem::path dest_dir;  // stage_dir or install_dir
  graph_state *state;
  std::string const *key;
};

void extract_all_archives(std::filesystem::path const &fetch_dir,
                          std::filesystem::path const &dest_dir,
                          int strip_components) {
  if (!std::filesystem::exists(fetch_dir)) {
    tui::trace("phase stage: fetch_dir does not exist, nothing to extract");
    return;
  }

  std::uint64_t total_files_extracted{ 0 };
  std::uint64_t total_files_copied{ 0 };

  for (auto const &entry : std::filesystem::directory_iterator(fetch_dir)) {
    if (!entry.is_regular_file()) { continue; }

    auto const &path{ entry.path() };
    std::string const filename{ path.filename().string() };

    // Skip envy-complete marker
    if (filename == "envy-complete") { continue; }

    if (extract_is_archive_extension(path)) {
      tui::trace("phase stage: extracting archive %s (strip=%d)",
                 filename.c_str(),
                 strip_components);

      extract_options opts{ .strip_components = strip_components };
      std::uint64_t const files{ extract(path, dest_dir, opts) };
      total_files_extracted += files;

      tui::trace("phase stage: extracted %llu files from %s",
                 static_cast<unsigned long long>(files),
                 filename.c_str());
    } else {
      tui::trace("phase stage: copying non-archive %s", filename.c_str());

      std::filesystem::path const dest_path{ dest_dir / filename };
      std::filesystem::copy_file(path,
                                 dest_path,
                                 std::filesystem::copy_options::overwrite_existing);
      ++total_files_copied;
    }
  }

  tui::trace(
      "phase stage: extraction complete (%llu files from archives, %llu files copied)",
      static_cast<unsigned long long>(total_files_extracted),
      static_cast<unsigned long long>(total_files_copied));
}

// Lua C function: ctx.extract_all({strip=0})
int lua_ctx_extract_all(lua_State *lua) {
  auto *ctx{ static_cast<stage_context *>(lua_touserdata(lua, lua_upvalueindex(1))) };
  if (!ctx) { return luaL_error(lua, "ctx.extract_all: missing context"); }

  int strip_components{ 0 };

  // Parse optional options table argument
  if (lua_gettop(lua) >= 1 && lua_istable(lua, 1)) {
    lua_getfield(lua, 1, "strip");
    if (lua_isnumber(lua, -1)) {
      strip_components = static_cast<int>(lua_tointeger(lua, -1));
      if (strip_components < 0) {
        return luaL_error(lua, "ctx.extract_all: strip must be non-negative");
      }
    } else if (!lua_isnil(lua, -1)) {
      return luaL_error(lua, "ctx.extract_all: strip must be a number");
    }
    lua_pop(lua, 1);  // Pop strip field
  }

  try {
    extract_all_archives(ctx->fetch_dir, ctx->dest_dir, strip_components);
  } catch (std::exception const &e) {
    return luaL_error(lua, "ctx.extract_all: %s", e.what());
  }

  return 0;  // No return values
}

void build_stage_context_table(lua_State *lua,
                               std::string const &identity,
                               std::unordered_map<std::string, lua_value> const &options,
                               stage_context *ctx) {
  lua_createtable(lua, 0, 10);  // Pre-allocate space for 10 fields

  lua_pushstring(lua, identity.c_str());
  lua_setfield(lua, -2, "identity");

  lua_createtable(lua, 0, static_cast<int>(options.size()));
  for (auto const &[key, val] : options) {
    value_to_lua_stack(lua, val);
    lua_setfield(lua, -2, key.c_str());
  }
  lua_setfield(lua, -2, "options");

  lua_pushstring(lua, ctx->fetch_dir.string().c_str());
  lua_setfield(lua, -2, "fetch_dir");

  lua_pushstring(lua, ctx->dest_dir.string().c_str());
  lua_setfield(lua, -2, "stage_dir");

  // Phase-specific: ctx.extract_all (convenience wrapper)
  lua_pushlightuserdata(lua, ctx);
  lua_pushcclosure(lua, lua_ctx_extract_all, 1);
  lua_setfield(lua, -2, "extract_all");

  // Common context bindings (all phases)
  lua_ctx_bindings_register_run(lua, ctx);
  lua_ctx_bindings_register_asset(lua, ctx);
  lua_ctx_bindings_register_copy(lua, ctx);
  lua_ctx_bindings_register_move(lua, ctx);
  lua_ctx_bindings_register_extract(lua, ctx);
}

std::filesystem::path determine_stage_destination(lua_State *lua,
                                                  cache::scoped_entry_lock const *lock) {
  lua_getglobal(lua, "stage");
  lua_getglobal(lua, "build");
  lua_getglobal(lua, "install");

  int const stage_type{ lua_type(lua, -3) };
  int const build_type{ lua_type(lua, -2) };
  int const install_type{ lua_type(lua, -1) };

  bool const has_custom_phases{ stage_type == LUA_TFUNCTION ||
                                build_type == LUA_TFUNCTION ||
                                install_type == LUA_TFUNCTION };

  lua_pop(lua, 3);

  std::filesystem::path const dest_dir{ has_custom_phases ? lock->stage_dir()
                                                          : lock->install_dir() };

  tui::trace("phase stage: destination=%s (custom_phases=%s)",
             dest_dir.string().c_str(),
             has_custom_phases ? "true" : "false");

  return dest_dir;
}

struct stage_options {
  int strip_components{ 0 };
};

stage_options parse_stage_options(lua_State *lua, std::string const &key) {
  stage_options opts;

  lua_getfield(lua, -1, "strip");
  if (lua_isnumber(lua, -1)) {
    opts.strip_components = static_cast<int>(lua_tointeger(lua, -1));
    if (opts.strip_components < 0) {
      lua_pop(lua, 2);
      throw std::runtime_error("stage.strip must be non-negative for " + key);
    }
  } else if (!lua_isnil(lua, -1)) {
    lua_pop(lua, 2);
    throw std::runtime_error("stage.strip must be a number for " + key);
  }
  lua_pop(lua, 1);  // Pop strip field

  return opts;
}

void run_default_stage(std::filesystem::path const &fetch_dir,
                       std::filesystem::path const &dest_dir) {
  tui::trace("phase stage: no stage field, running default extraction");
  extract_all_archives(fetch_dir, dest_dir, 0);
}

void run_declarative_stage(lua_State *lua,
                           std::filesystem::path const &fetch_dir,
                           std::filesystem::path const &dest_dir,
                           std::string const &key) {
  stage_options const opts{ parse_stage_options(lua, key) };
  lua_pop(lua, 1);  // Pop stage table

  tui::trace("phase stage: declarative extraction with strip=%d", opts.strip_components);
  extract_all_archives(fetch_dir, dest_dir, opts.strip_components);
}

void run_programmatic_stage(lua_State *lua,
                            std::filesystem::path const &fetch_dir,
                            std::filesystem::path const &dest_dir,
                            std::string const &identity,
                            std::unordered_map<std::string, lua_value> const &options,
                            graph_state &state,
                            std::string const &key) {
  tui::trace("phase stage: running imperative stage function");

  stage_context ctx{ .fetch_dir = fetch_dir,
                     .dest_dir = dest_dir,
                     .state = &state,
                     .key = &key };

  build_stage_context_table(lua, identity, options, &ctx);

  // Stack: stage_function at -2, ctx_table at -1 (ready for pcall)
  if (lua_pcall(lua, 1, 0, 0) != LUA_OK) {
    char const *err{ lua_tostring(lua, -1) };
    std::string error_msg{ err ? err : "unknown error" };
    lua_pop(lua, 1);
    throw std::runtime_error("Stage function failed for " + key + ": " + error_msg);
  }
}

void run_shell_stage(std::string_view script,
                     std::filesystem::path const &dest_dir,
                     std::string const &key) {
  tui::trace("phase stage: running shell script");

  shell_env_t env{ shell_getenv() };

  std::vector<std::string> output_lines;
  shell_run_cfg inv{ .on_output_line =
                         [&](std::string_view line) {
                           tui::info("%s", std::string{ line }.c_str());
                           output_lines.emplace_back(line);
                         },
                     .cwd = dest_dir,
                     .env = std::move(env),
                     .shell = shell_parse_choice(std::nullopt) };

  shell_result const result{ shell_run(script, inv) };

  if (result.exit_code != 0) {
    if (result.signal) {
      throw std::runtime_error("Stage shell script failed for " + key +
                               " (terminated by signal " + std::to_string(*result.signal) +
                               ")");
    } else {
      throw std::runtime_error("Stage shell script failed for " + key + " (exit code " +
                               std::to_string(result.exit_code) + ")");
    }
  }
}

}  // namespace

void run_stage_phase(std::string const &key, graph_state &state) {
  tui::trace("phase stage START %s", key.c_str());
  trace_on_exit trace_end{ "phase stage END " + key };

  auto [lua, lock, identity, options] = [&] {
    typename decltype(state.recipes)::accessor acc;
    if (!state.recipes.find(acc, key)) {
      throw std::runtime_error("Recipe not found for " + key);
    }
    return std::tuple{ acc->second.lua_state.get(),
                       acc->second.lock.get(),
                       acc->second.identity,
                       acc->second.options };
  }();

  if (!lock) {
    throw std::runtime_error("BUG: stage phase executing without lock for " + key);
  }

  std::filesystem::path const dest_dir{ determine_stage_destination(lua, lock) };
  std::filesystem::path const fetch_dir{ lock->fetch_dir() };

  lua_getglobal(lua, "stage");
  int const stage_type{ lua_type(lua, -1) };

  switch (stage_type) {
    case LUA_TNIL:
      lua_pop(lua, 1);
      run_default_stage(fetch_dir, dest_dir);
      break;

    case LUA_TSTRING: {
      size_t len{ 0 };
      char const *script{ lua_tolstring(lua, -1, &len) };
      std::string_view script_view{ script, len };
      lua_pop(lua, 1);
      run_shell_stage(script_view, dest_dir, key);
      break;
    }

    case LUA_TFUNCTION:
      run_programmatic_stage(lua, fetch_dir, dest_dir, identity, options, state, key);
      break;

    case LUA_TTABLE: run_declarative_stage(lua, fetch_dir, dest_dir, key); break;

    default:
      lua_pop(lua, 1);
      throw std::runtime_error("stage field must be nil, string, table, or function for " +
                               key);
  }
}

}  // namespace envy
