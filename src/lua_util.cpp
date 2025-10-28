#include "lua_util.h"

#include "tui.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

#include <sstream>

namespace envy {
namespace {

int lua_print_override(lua_State *lua) {
  int argc{ lua_gettop(lua) };
  std::ostringstream oss;

  for (int i{ 1 }; i <= argc; ++i) {
    if (i > 1) { oss << '\t'; }
    if (auto str{ luaL_tolstring(lua, i, nullptr) }) { oss << str; }
    lua_pop(lua, 1);
  }

  tui::info("%s", oss.str().c_str());
  return 0;
}

template <void tui_func(char const *, ...)>
int lua_print_tui(lua_State *lua) {
  tui_func("%s", luaL_checkstring(lua, 1));
  return 0;
}

}  // namespace

lua_state_ptr lua_make() {
  lua_State *L{ luaL_newstate() };
  if (!L) {
    tui::error("Failed to create Lua state");
    return { nullptr, lua_close };
  }

  luaL_openlibs(L);

  return { L, lua_close };
}

void lua_add_tui(lua_state_ptr const &state) {
  lua_State *L{ state.get() };
  if (!L) {
    tui::error("lua_add_tui called with null state");
    return;
  }

  // Override print to use tui::info
  lua_pushcfunction(L, lua_print_override);
  lua_setglobal(L, "print");

  // Create envy table with logging + stdout
  lua_newtable(L);
  lua_pushcfunction(L, lua_print_tui<tui::debug>);
  lua_setfield(L, -2, "debug");
  lua_pushcfunction(L, lua_print_tui<tui::info>);
  lua_setfield(L, -2, "info");
  lua_pushcfunction(L, lua_print_tui<tui::warn>);
  lua_setfield(L, -2, "warn");
  lua_pushcfunction(L, lua_print_tui<tui::error>);
  lua_setfield(L, -2, "error");
  lua_pushcfunction(L, lua_print_tui<tui::print_stdout>);
  lua_setfield(L, -2, "stdout");
  lua_setglobal(L, "envy");
}

bool lua_run_file(lua_state_ptr const &state, std::filesystem::path const &path) {
  lua_State *L{ state.get() };
  if (!L) {
    tui::error("lua_run called with null state");
    return false;
  }

  if (int load_status{ luaL_loadfile(L, path.string().c_str()) }; load_status != LUA_OK) {
    char const *err{ lua_tostring(L, -1) };
    if (load_status == LUA_ERRFILE) {
      tui::error("Failed to open %s: %s",
                 path.string().c_str(),
                 err ? err : "unknown error");
    } else {
      tui::error("%s", err ? err : "unknown error");
    }
    return false;
  }

  if (lua_pcall(L, 0, LUA_MULTRET, 0) != LUA_OK) {
    auto err{ lua_tostring(L, -1) };
    tui::error("%s", err ? err : "unknown error");
    return false;
  }

  return true;
}

bool lua_run_string(lua_state_ptr const &state, char const *script) {
  lua_State *L{ state.get() };
  if (!L) {
    tui::error("lua_run_string called with null state");
    return false;
  }

  if (luaL_loadstring(L, script) != LUA_OK) {
    char const *err{ lua_tostring(L, -1) };
    tui::error("Failed to load Lua script: %s", err ? err : "unknown error");
    return false;
  }

  if (lua_pcall(L, 0, LUA_MULTRET, 0) != LUA_OK) {
    char const *err{ lua_tostring(L, -1) };
    tui::error("Lua script execution failed: %s", err ? err : "unknown error");
    return false;
  }

  return true;
}

}  // namespace envy
