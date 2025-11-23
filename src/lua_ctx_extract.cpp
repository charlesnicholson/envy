#include "lua_ctx_bindings.h"

#include "extract.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

#include <cstdint>
#include <filesystem>

namespace envy {

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

}  // namespace envy
