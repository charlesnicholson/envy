#include "phase_build.h"

#include "lua_ctx_bindings.h"
#include "lua_util.h"
#include "shell.h"
#include "tui.h"

extern "C" {
#include "lua.h"
}

#include <filesystem>
#include <stdexcept>
#include <string>
#include <tuple>

namespace envy {
namespace {

// Context data for Lua C functions (stored as userdata upvalue)
struct build_context : lua_ctx_common {
  // run_dir inherited from base is stage_dir (build working directory)
  std::filesystem::path install_dir;  // Additional field for build phase
};

void build_build_context_table(lua_State *lua,
                               std::string const &identity,
                               std::unordered_map<std::string, lua_value> const &options,
                               build_context *ctx) {
  lua_createtable(lua, 0, 9);  // Pre-allocate space for 9 fields

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

  lua_pushstring(lua, ctx->run_dir.string().c_str());
  lua_setfield(lua, -2, "stage_dir");

  lua_pushstring(lua, ctx->install_dir.string().c_str());
  lua_setfield(lua, -2, "install_dir");

  // Common context bindings (all phases)
  lua_ctx_bindings_register_run(lua, ctx);
  lua_ctx_bindings_register_asset(lua, ctx);
  lua_ctx_bindings_register_copy(lua, ctx);
  lua_ctx_bindings_register_move(lua, ctx);
  lua_ctx_bindings_register_extract(lua, ctx);
}

void run_programmatic_build(lua_State *lua,
                            std::filesystem::path const &fetch_dir,
                            std::filesystem::path const &stage_dir,
                            std::filesystem::path const &install_dir,
                            std::string const &identity,
                            std::unordered_map<std::string, lua_value> const &options,
                            graph_state &state,
                            std::string const &key) {
  tui::trace("phase build: running programmatic build function");

  build_context ctx{};
  ctx.fetch_dir = fetch_dir;
  ctx.run_dir = stage_dir;
  ctx.state = &state;
  ctx.key = &key;
  ctx.manifest_ = state.manifest_;
  ctx.install_dir = install_dir;

  build_build_context_table(lua, identity, options, &ctx);

  // Stack: build_function at -2, ctx_table at -1 (ready for pcall)
  if (lua_pcall(lua, 1, 0, 0) != LUA_OK) {
    char const *err{ lua_tostring(lua, -1) };
    std::string error_msg{ err ? err : "unknown error" };
    lua_pop(lua, 1);
    throw std::runtime_error("Build function failed for " + key + ": " + error_msg);
  }
}

void run_shell_build(std::string_view script,
                     std::filesystem::path const &stage_dir,
                     std::string const &key) {
  tui::trace("phase build: running shell script");

  shell_env_t env{ shell_getenv() };

  std::vector<std::string> output_lines;
  shell_run_cfg inv{ .on_output_line =
                         [&](std::string_view line) {
                           tui::info("%s", std::string{ line }.c_str());
                           output_lines.emplace_back(line);
                         },
                     .cwd = stage_dir,
                     .env = std::move(env),
                     .shell = shell_parse_choice(std::nullopt) };

  shell_result const result{ shell_run(script, inv) };

  if (result.exit_code != 0) {
    if (result.signal) {
      throw std::runtime_error("Build shell script terminated by signal " +
                               std::to_string(*result.signal) + " for " + key);
    } else {
      throw std::runtime_error("Build shell script failed for " + key + " (exit code " +
                               std::to_string(result.exit_code) + ")");
    }
  }
}

}  // namespace

void run_build_phase(std::string const &key, graph_state &state) {
  tui::trace("phase build START %s", key.c_str());
  trace_on_exit trace_end{ "phase build END " + key };

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
    throw std::runtime_error("BUG: build phase executing without lock for " + key);
  }

  std::filesystem::path const fetch_dir{ lock->fetch_dir() };
  std::filesystem::path const stage_dir{ lock->stage_dir() };
  std::filesystem::path const install_dir{ lock->install_dir() };

  lua_getglobal(lua, "build");

  switch (lua_type(lua, -1)) {
    case LUA_TNIL:
      lua_pop(lua, 1);
      tui::trace("phase build: no build field, skipping");
      break;

    case LUA_TSTRING: {
      size_t len{ 0 };
      char const *script{ lua_tolstring(lua, -1, &len) };
      std::string script_str{ script, len };  // Copy before popping
      lua_pop(lua, 1);
      run_shell_build(script_str, stage_dir, key);
      break;
    }

    case LUA_TFUNCTION:
      run_programmatic_build(lua,
                             fetch_dir,
                             stage_dir,
                             install_dir,
                             identity,
                             options,
                             state,
                             key);
      break;

    default:
      lua_pop(lua, 1);
      throw std::runtime_error("build field must be nil, string, or function for " + key);
  }
}

}  // namespace envy
