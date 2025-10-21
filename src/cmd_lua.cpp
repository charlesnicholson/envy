#include "cmd_lua.h"
#include "tui.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

#include <sstream>

namespace envy {
namespace {

int lua_print_override(lua_State *L) {
  int argc{ lua_gettop(L) };
  std::ostringstream oss;

  for (int i{ 1 }; i <= argc; ++i) {
    if (i > 1) { oss << '\t'; }
    char const *str{ luaL_tolstring(L, i, nullptr) };
    if (str) { oss << str; }
    lua_pop(L, 1);
  }

  tui::info("%s", oss.str().c_str());
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
  succeeded_ = true;

  node_.emplace(g, [this](tbb::flow::continue_msg const &) {
    auto const script_path{ cfg_.script_path };
    lua_State *L{ luaL_newstate() };
    if (!L) {
      tui::error("Failed to create Lua state");
      succeeded_ = false;
      return;
    }

    setup_lua_environment(L);

    if (int const load_status{ luaL_loadfile(L, script_path.string().c_str()) }; load_status != LUA_OK) {
      char const *err{ lua_tostring(L, -1) };
      if (load_status == LUA_ERRFILE) {
        tui::error("Failed to open %s: %s",
                   script_path.string().c_str(),
                   err ? err : "unknown error");
      } else {
        tui::error("%s", err ? err : "unknown error");
      }
      succeeded_ = false;
      lua_close(L);
      return;
    }

    if (lua_pcall(L, 0, LUA_MULTRET, 0) != LUA_OK) {
      char const *err{ lua_tostring(L, -1) };
      tui::error("%s", err ? err : "unknown error");
      succeeded_ = false;
      lua_close(L);
      return;
    }

    lua_close(L);
  });

  node_->try_put(tbb::flow::continue_msg{});
}

}  // namespace envy
