#include "lua_ctx_bindings.h"

#include "tui.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

#include <filesystem>

namespace envy {

// Lua C function: ctx.ls(path)
int lua_ctx_ls(lua_State *lua) {
  auto const *ctx{ static_cast<lua_ctx_common *>(
      lua_touserdata(lua, lua_upvalueindex(1))) };
  if (!ctx) { return luaL_error(lua, "ctx.ls: missing context"); }

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

}  // namespace envy
