#include "lua_ctx_bindings.h"

#include "sha256.h"
#include "tui.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace envy {

namespace {

struct commit_entry {
  std::string filename;
  std::string sha256;  // empty = no verification
};

// Parse ctx.commit_fetch() arguments into commit entries
std::vector<commit_entry> parse_commit_fetch_args(lua_State *lua) {
  std::vector<commit_entry> entries;
  int const arg_type{ lua_type(lua, 1) };

  switch (arg_type) {
    case LUA_TSTRING:
      // Scalar string
      entries.push_back({ lua_tostring(lua, 1), "" });
      break;

    case LUA_TTABLE: {
      // Check if array or single table
      lua_rawgeti(lua, 1, 1);
      int const first_elem_type{ lua_type(lua, -1) };
      lua_pop(lua, 1);

      switch (first_elem_type) {
        case LUA_TNIL: {
          // Single table {filename="...", sha256="..."}
          lua_getfield(lua, 1, "filename");
          if (!lua_isstring(lua, -1)) {
            throw std::runtime_error("table missing 'filename' field");
          }
          std::string filename{ lua_tostring(lua, -1) };
          lua_pop(lua, 1);

          lua_getfield(lua, 1, "sha256");
          std::string sha256;
          if (lua_isstring(lua, -1)) { sha256 = lua_tostring(lua, -1); }
          lua_pop(lua, 1);

          entries.push_back({ std::move(filename), std::move(sha256) });
          break;
        }

        case LUA_TSTRING: {
          // Array of strings {"file1", "file2"}
          size_t const len{ lua_rawlen(lua, 1) };
          for (size_t i = 1; i <= len; ++i) {
            lua_rawgeti(lua, 1, i);
            if (!lua_isstring(lua, -1)) {
              throw std::runtime_error("array element " + std::to_string(i) +
                                       " must be string");
            }
            entries.push_back({ lua_tostring(lua, -1), "" });
            lua_pop(lua, 1);
          }
          break;
        }

        case LUA_TTABLE: {
          // Array of tables {{filename="...", sha256="..."}, {...}}
          size_t const len{ lua_rawlen(lua, 1) };
          for (size_t i = 1; i <= len; ++i) {
            lua_rawgeti(lua, 1, i);

            lua_getfield(lua, -1, "filename");
            if (!lua_isstring(lua, -1)) {
              throw std::runtime_error("array element " + std::to_string(i) +
                                       " missing 'filename' field");
            }
            std::string filename{ lua_tostring(lua, -1) };
            lua_pop(lua, 1);

            lua_getfield(lua, -1, "sha256");
            std::string sha256;
            if (lua_isstring(lua, -1)) { sha256 = lua_tostring(lua, -1); }
            lua_pop(lua, 1);

            entries.push_back({ std::move(filename), std::move(sha256) });
            lua_pop(lua, 1);  // pop table
          }
          break;
        }

        default: throw std::runtime_error("invalid array element type");
      }
      break;
    }

    default: throw std::runtime_error("argument must be string or table");
  }

  return entries;
}

// Verify SHA256 and move files from tmp_dir to fetch_dir
void commit_files(std::vector<commit_entry> const &entries,
                  std::filesystem::path const &tmp_dir,
                  std::filesystem::path const &fetch_dir) {
  std::vector<std::string> errors;

  for (auto const &entry : entries) {
    std::filesystem::path const src{ tmp_dir / entry.filename };
    std::filesystem::path const dest{ fetch_dir / entry.filename };

    // Check source exists
    if (!std::filesystem::exists(src)) {
      errors.push_back(entry.filename + ": file not found in tmp directory");
      continue;
    }

    // Verify SHA256 if provided
    if (!entry.sha256.empty()) {
      try {
        tui::debug("ctx.commit_fetch: verifying SHA256 for %s", entry.filename.c_str());
        sha256_verify(entry.sha256, sha256(src));
      } catch (std::exception const &e) {
        errors.push_back(entry.filename + ": " + e.what());
        continue;
      }
    }

    // Move file
    try {
      std::filesystem::rename(src, dest);
      tui::debug("ctx.commit_fetch: moved %s to fetch_dir", entry.filename.c_str());
    } catch (std::exception const &e) {
      errors.push_back(entry.filename + ": failed to move: " + e.what());
    }
  }

  if (!errors.empty()) {
    std::ostringstream oss;
    oss << "ctx.commit_fetch failed:\n";
    for (auto const &err : errors) { oss << "  " << err << "\n"; }
    throw std::runtime_error(oss.str());
  }
}

}  // namespace

// Lua C function: ctx.commit_fetch(filename) or ctx.commit_fetch({filename, sha256})
// Moves file(s) from ctx.tmp to fetch_dir with optional SHA256 verification
int lua_ctx_commit_fetch(lua_State *lua) {
  auto *ctx{ static_cast<fetch_phase_ctx *>(lua_touserdata(lua, lua_upvalueindex(1))) };
  if (!ctx) { return luaL_error(lua, "ctx.commit_fetch: missing context"); }

  try {
    commit_files(parse_commit_fetch_args(lua), ctx->run_dir, ctx->fetch_dir);
  } catch (std::exception const &e) {
    return luaL_error(lua, "ctx.commit_fetch: %s", e.what());
  }

  return 0;  // No return values
}

}  // namespace envy
