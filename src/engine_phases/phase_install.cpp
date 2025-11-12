#include "phase_install.h"

#include "../lua_ctx_bindings.h"
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
#include <string_view>

namespace envy {
namespace {

struct install_context : lua_ctx_common {
  std::filesystem::path install_dir;
  std::filesystem::path stage_dir;
  cache::scoped_entry_lock *lock{ nullptr };
};

bool directory_has_entries(std::filesystem::path const &dir) {
  std::error_code ec;
  if (!std::filesystem::exists(dir, ec) || ec) { return false; }

  std::filesystem::directory_iterator it{ dir, ec };
  if (ec) {
    throw std::runtime_error("Failed to enumerate directory " + dir.string() + ": " +
                             ec.message());
  }

  std::filesystem::directory_iterator end_iter;
  for (; it != end_iter; ++it) { return true; }

  return false;
}

int lua_ctx_mark_install_complete(lua_State *lua) {
  auto *ctx{ static_cast<install_context *>(lua_touserdata(lua, lua_upvalueindex(1))) };
  if (!ctx || !ctx->lock) {
    return luaL_error(lua, "ctx.mark_install_complete: missing install context");
  }

  ctx->lock->mark_install_complete();
  return 0;
}

void build_install_context_table(lua_State *lua,
                                 std::string const &identity,
                                 std::unordered_map<std::string, lua_value> const &options,
                                 install_context *ctx) {
  lua_createtable(lua, 0, 11);

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

  lua_pushstring(lua, ctx->stage_dir.string().c_str());
  lua_setfield(lua, -2, "stage_dir");

  lua_pushstring(lua, ctx->install_dir.string().c_str());
  lua_setfield(lua, -2, "install_dir");

  lua_pushlightuserdata(lua, ctx);
  lua_pushcclosure(lua, lua_ctx_mark_install_complete, 1);
  lua_setfield(lua, -2, "mark_install_complete");

  lua_ctx_bindings_register_run(lua, ctx);
  lua_ctx_bindings_register_asset(lua, ctx);
  lua_ctx_bindings_register_copy(lua, ctx);
  lua_ctx_bindings_register_move(lua, ctx);
  lua_ctx_bindings_register_extract(lua, ctx);
}

bool run_programmatic_install(lua_State *lua,
                              cache::scoped_entry_lock *lock,
                              std::filesystem::path const &fetch_dir,
                              std::filesystem::path const &stage_dir,
                              std::filesystem::path const &install_dir,
                              std::string const &identity,
                              std::unordered_map<std::string, lua_value> const &options,
                              graph_state &state,
                              std::string const &key) {
  tui::trace("phase install: running programmatic install function");

  install_context ctx{};
  ctx.fetch_dir = fetch_dir;
  ctx.run_dir = install_dir;
  ctx.state = &state;
  ctx.key = &key;
  ctx.manifest_ = state.manifest_;
  ctx.install_dir = install_dir;
  ctx.stage_dir = stage_dir;
  ctx.lock = lock;

  build_install_context_table(lua, identity, options, &ctx);

  if (lua_pcall(lua, 1, 0, 0) != LUA_OK) {
    char const *err{ lua_tostring(lua, -1) };
    std::string error_msg{ err ? err : "unknown error" };
    lua_pop(lua, 1);
    throw std::runtime_error("Install function failed for " + key + ": " + error_msg);
  }

  return lock->is_install_complete();
}

bool run_shell_install(std::string_view script,
                       std::filesystem::path const &install_dir,
                       cache::scoped_entry_lock *lock,
                       std::string const &key) {
  tui::trace("phase install: running shell script");

  shell_env_t env{ shell_getenv() };
  shell_run_cfg cfg{
    .on_output_line =
        [&](std::string_view line) { tui::info("%s", std::string{ line }.c_str()); },
    .cwd = install_dir,
    .env = std::move(env),
    .shell = shell_parse_choice(std::nullopt),
  };

  shell_result const result{ shell_run(script, cfg) };

  if (result.exit_code != 0) {
    if (result.signal) {
      throw std::runtime_error("Install shell script terminated by signal " +
                               std::to_string(*result.signal) + " for " + key);
    }
    throw std::runtime_error("Install shell script failed for " + key + " (exit code " +
                             std::to_string(result.exit_code) + ")");
  }

  lock->mark_install_complete();
  return true;
}

bool promote_stage_to_install(cache::scoped_entry_lock *lock) {
  auto const install_dir{ lock->install_dir() };
  auto const stage_dir{ lock->stage_dir() };

  if (directory_has_entries(install_dir)) {
    tui::trace("phase install: install_dir already populated, marking complete");
    lock->mark_install_complete();
    return true;
  }

  if (directory_has_entries(stage_dir)) {
    tui::trace("phase install: promoting stage_dir contents to install_dir");
    std::filesystem::remove_all(install_dir);
    std::filesystem::create_directories(install_dir.parent_path());
    std::filesystem::rename(stage_dir, install_dir);
    lock->mark_install_complete();
    return true;
  }

  tui::trace("phase install: no outputs detected, leaving entry unmarked");
  return false;
}

}  // namespace

void run_install_phase(std::string const &key, graph_state &state) {
  tui::trace("phase install START %s", key.c_str());
  trace_on_exit trace_end{ "phase install END " + key };

  typename decltype(state.recipes)::accessor acc;
  if (!state.recipes.find(acc, key)) {
    throw std::runtime_error("Recipe not found for " + key);
  }

  if (!acc->second.lock) {
    throw std::runtime_error("BUG: install phase executing without lock for " + key);
  }

  cache::scoped_entry_lock::ptr_t lock{ std::move(acc->second.lock) };
  std::filesystem::path const fetch_dir{ lock->fetch_dir() };
  std::filesystem::path const stage_dir{ lock->stage_dir() };
  std::filesystem::path const install_dir{ lock->install_dir() };
  std::filesystem::path const final_asset_path{ install_dir.parent_path() / "asset" };

  lua_State *lua{ acc->second.lua_state.get() };
  lua_getglobal(lua, "install");
  int const install_type{ lua_type(lua, -1) };

  bool marked_complete{ false };

  switch (install_type) {
    case LUA_TNIL:
      lua_pop(lua, 1);
      marked_complete = promote_stage_to_install(lock.get());
      break;

    case LUA_TSTRING: {
      size_t len{ 0 };
      std::string script{ lua_tolstring(lua, -1, &len), len };
      lua_pop(lua, 1);
      marked_complete = run_shell_install(script, install_dir, lock.get(), key);
      break;
    }

    case LUA_TFUNCTION:
      marked_complete = run_programmatic_install(lua,
                                                 lock.get(),
                                                 fetch_dir,
                                                 stage_dir,
                                                 install_dir,
                                                 acc->second.identity,
                                                 acc->second.options,
                                                 state,
                                                 key);
      break;

    default:
      lua_pop(lua, 1);
      throw std::runtime_error("install field must be nil, string, or function for " +
                               key);
  }

  if (marked_complete) { acc->second.asset_path = final_asset_path; }
}

}  // namespace envy
