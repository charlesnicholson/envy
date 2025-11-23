#include "lua_ctx_bindings.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

#include <filesystem>

namespace envy {

// Lua C function: ctx.copy(src, dst)
int lua_ctx_copy(lua_State *lua) {
  auto const *ctx{ static_cast<lua_ctx_common *>(
      lua_touserdata(lua, lua_upvalueindex(1))) };
  if (!ctx) { return luaL_error(lua, "ctx.copy: missing context"); }

  // Arg 1: source path (required)
  if (!lua_isstring(lua, 1)) {
    return luaL_error(lua, "ctx.copy: first argument must be source path string");
  }
  char const *src_str{ lua_tostring(lua, 1) };

  // Arg 2: destination path (required)
  if (!lua_isstring(lua, 2)) {
    return luaL_error(lua, "ctx.copy: second argument must be destination path string");
  }
  char const *dst_str{ lua_tostring(lua, 2) };

  std::filesystem::path src{ src_str };
  std::filesystem::path dst{ dst_str };

  // Resolve relative paths against run_dir
  if (src.is_relative()) { src = ctx->run_dir / src; }
  if (dst.is_relative()) { dst = ctx->run_dir / dst; }

  try {
    if (!std::filesystem::exists(src)) {
      return luaL_error(lua, "ctx.copy: source not found: %s", src_str);
    }

    // Auto-detect file vs directory
    if (std::filesystem::is_directory(src)) {
      std::filesystem::copy(src,
                            dst,
                            std::filesystem::copy_options::recursive |
                                std::filesystem::copy_options::overwrite_existing);
    } else {
      // Ensure parent directory exists for file copy
      if (dst.has_parent_path()) {
        std::filesystem::create_directories(dst.parent_path());
      }
      std::filesystem::copy_file(src,
                                 dst,
                                 std::filesystem::copy_options::overwrite_existing);
    }

    return 0;  // No return values
  } catch (std::exception const &e) { return luaL_error(lua, "ctx.copy: %s", e.what()); }
}

}  // namespace envy
