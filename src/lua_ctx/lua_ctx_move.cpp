#include "lua_ctx_bindings.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

#include <filesystem>

namespace envy {

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

    // If src is a file and dst is an existing directory, move file into directory
    if (std::filesystem::is_regular_file(src) && std::filesystem::is_directory(dst)) {
      dst = dst / src.filename();
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

}  // namespace envy
