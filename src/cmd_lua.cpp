#include "cmd_lua.h"
#include "tui.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

#include <cstdio>

namespace envy {
namespace {

int lua_print_override(lua_State *L) {
  int argc{ lua_gettop(L) };

  for (int i{ 1 }; i <= argc; ++i) {
    char const *str{ luaL_tolstring(L, i, nullptr) };
    if (str) { tui::info("%s", str); }
    lua_pop(L, 1);
  }

  return 0;
}

int lua_envy_debug(lua_State *L) {
  char const *msg{ luaL_checkstring(L, 1) };
  tui::debug("%s", msg);
  return 0;
}

int lua_envy_info(lua_State *L) {
  char const *msg{ luaL_checkstring(L, 1) };
  tui::info("%s", msg);
  return 0;
}

int lua_envy_warn(lua_State *L) {
  char const *msg{ luaL_checkstring(L, 1) };
  tui::warn("%s", msg);
  return 0;
}

int lua_envy_error(lua_State *L) {
  char const *msg{ luaL_checkstring(L, 1) };
  tui::error("%s", msg);
  return 0;
}

int lua_envy_stdout(lua_State *L) {
  char const *msg{ luaL_checkstring(L, 1) };
  tui::print_stdout("%s", msg);
  return 0;
}

void setup_lua_environment(lua_State *L) {
  luaL_openlibs(L);

  lua_pushcfunction(L, lua_print_override);
  lua_setglobal(L, "print");

  lua_newtable(L);
  lua_pushcfunction(L, lua_envy_debug);
  lua_setfield(L, -2, "debug");
  lua_pushcfunction(L, lua_envy_info);
  lua_setfield(L, -2, "info");
  lua_pushcfunction(L, lua_envy_warn);
  lua_setfield(L, -2, "warn");
  lua_pushcfunction(L, lua_envy_error);
  lua_setfield(L, -2, "error");
  lua_pushcfunction(L, lua_envy_stdout);
  lua_setfield(L, -2, "stdout");
  lua_setglobal(L, "envy");
}

}  // anonymous namespace

cmd_lua::cmd_lua(cmd_lua::cfg cfg) : cfg_{ std::move(cfg) } {}

void cmd_lua::schedule(tbb::flow::graph &g) {
  node_.emplace(g, [script_path = cfg_.script_path](tbb::flow::continue_msg const &) {
    lua_State *L{ luaL_newstate() };
    if (!L) {
      tui::error("Failed to create Lua state");
      return;
    }

    setup_lua_environment(L);

    if (luaL_loadfile(L, script_path.string().c_str()) != LUA_OK) {
      char const *err{ lua_tostring(L, -1) };
      tui::error("Failed to load %s: %s",
                 script_path.string().c_str(),
                 err ? err : "unknown error");
      lua_close(L);
      return;
    }

    if (lua_pcall(L, 0, LUA_MULTRET, 0) != LUA_OK) {
      char const *err{ lua_tostring(L, -1) };
      tui::error("Script error: %s", err ? err : "unknown error");
      lua_close(L);
      return;
    }

    lua_close(L);
  });

  node_->try_put(tbb::flow::continue_msg{});
}

}  // namespace envy
