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

template <void tui_func(char const *, ...)>
int lua_print_tui(lua_State *lua) {
  char const *msg{ luaL_checkstring(lua, 1) };
  tui_func("%s", msg);
  return 0;
}

void setup_lua_environment(lua_State *lua) {
  luaL_openlibs(lua);

  lua_pushcfunction(lua, lua_print_override);
  lua_setglobal(lua, "print");
  lua_newtable(lua);
  lua_pushcfunction(lua, lua_print_tui<tui::debug>);
  lua_setfield(lua, -2, "debug");
  lua_pushcfunction(lua, lua_print_tui<tui::info>);
  lua_setfield(lua, -2, "info");
  lua_pushcfunction(lua, lua_print_tui<tui::warn>);
  lua_setfield(lua, -2, "warn");
  lua_pushcfunction(lua, lua_print_tui<tui::error>);
  lua_setfield(lua, -2, "error");
  lua_pushcfunction(lua, lua_print_tui<tui::print_stdout>);
  lua_setfield(lua, -2, "stdout");
  lua_setglobal(lua, "envy");
}

}  // anonymous namespace

cmd_lua::cmd_lua(cmd_lua::cfg cfg) : cfg_{ std::move(cfg) } {}

void cmd_lua::schedule(tbb::flow::graph &g) {
  succeeded_ = true;

  node_.emplace(g, [this](tbb::flow::continue_msg const &) {
    lua_State *lua{ luaL_newstate() };

    do {
      if (!lua) {
        tui::error("Failed to create Lua state");
        succeeded_ = false;
        break;
      }

      setup_lua_environment(lua);

      if (int const load_status{ luaL_loadfile(lua, cfg_.script_path.string().c_str()) };
          load_status != LUA_OK) {
        char const *err{ lua_tostring(lua, -1) };

        if (load_status == LUA_ERRFILE) {
          tui::error("Failed to open %s: %s",
                     cfg_.script_path.string().c_str(),
                     err ? err : "unknown error");
        } else {
          tui::error("%s", err ? err : "unknown error");
        }

        succeeded_ = false;
        break;
      }

      if (lua_pcall(lua, 0, LUA_MULTRET, 0) != LUA_OK) {
        char const *err{ lua_tostring(lua, -1) };
        tui::error("%s", err ? err : "unknown error");
        succeeded_ = false;
        break;
      }
    } while (0);

    if (lua) { lua_close(lua); }
  });

  node_->try_put(tbb::flow::continue_msg{});
}

}  // namespace envy
