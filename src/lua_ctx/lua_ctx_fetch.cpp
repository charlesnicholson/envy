#include "lua_ctx_bindings.h"

#include "fetch.h"
#include "recipe.h"
#include "trace.h"
#include "tui.h"
#include "uri.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

#include <chrono>
#include <filesystem>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace envy {

// Extern declaration (defined in phase_fetch.cpp)
fetch_request url_to_fetch_request(std::string const &url,
                                   std::filesystem::path const &dest,
                                   std::optional<std::string> const &ref,
                                   std::string const &context);

// Lua C function: ctx.fetch(url) or ctx.fetch({url1, url2}) or ctx.fetch({url="..."})
// Returns: basename string (scalar) or array of basenames (array)
// Never does SHA256 verification - that's for ctx.commit_fetch()
int lua_ctx_fetch(lua_State *lua) {
  auto *ctx{ static_cast<fetch_phase_ctx *>(lua_touserdata(lua, lua_upvalueindex(1))) };
  if (!ctx) { return luaL_error(lua, "ctx.fetch: missing context"); }

  std::vector<std::string> urls;
  std::vector<std::optional<std::string>> refs;
  std::vector<std::string> basenames;
  bool is_array{ false };

  // Parse argument: string, string array, table, or table array
  int const arg_type{ lua_type(lua, 1) };

  switch (arg_type) {
    case LUA_TSTRING:
      // Scalar string
      urls.push_back(lua_tostring(lua, 1));
      refs.push_back(std::nullopt);
      break;

    case LUA_TTABLE: {
      // Check if array or single table
      lua_rawgeti(lua, 1, 1);
      int const first_elem_type{ lua_type(lua, -1) };
      lua_pop(lua, 1);

      switch (first_elem_type) {
        case LUA_TNIL: {
          // Single table {source="...", ref="..."}
          lua_getfield(lua, 1, "source");
          if (!lua_isstring(lua, -1)) {
            return luaL_error(lua, "ctx.fetch: table missing 'source' field");
          }
          urls.push_back(lua_tostring(lua, -1));
          lua_pop(lua, 1);

          lua_getfield(lua, 1, "ref");
          if (lua_isstring(lua, -1)) {
            refs.push_back(lua_tostring(lua, -1));
          } else {
            refs.push_back(std::nullopt);
          }
          lua_pop(lua, 1);
          break;
        }

        case LUA_TSTRING: {
          // Array of strings {"url1", "url2"}
          is_array = true;
          size_t const len{ lua_rawlen(lua, 1) };
          for (size_t i = 1; i <= len; ++i) {
            lua_rawgeti(lua, 1, i);
            if (!lua_isstring(lua, -1)) {
              return luaL_error(lua, "ctx.fetch: array element %zu must be string", i);
            }
            urls.push_back(lua_tostring(lua, -1));
            refs.push_back(std::nullopt);
            lua_pop(lua, 1);
          }
          break;
        }

        case LUA_TTABLE: {
          // Array of tables {{source="...", ref="..."}, {...}}
          is_array = true;
          size_t const len{ lua_rawlen(lua, 1) };
          for (size_t i = 1; i <= len; ++i) {
            lua_rawgeti(lua, 1, i);
            lua_getfield(lua, -1, "source");
            if (!lua_isstring(lua, -1)) {
              return luaL_error(lua,
                                "ctx.fetch: array element %zu missing 'source' field",
                                i);
            }
            urls.push_back(lua_tostring(lua, -1));
            lua_pop(lua, 1);  // pop source string

            lua_getfield(lua, -1, "ref");
            if (lua_isstring(lua, -1)) {
              refs.push_back(lua_tostring(lua, -1));
            } else {
              refs.push_back(std::nullopt);
            }
            lua_pop(lua, 2);  // pop ref string/nil and table
          }
          break;
        }

        default: return luaL_error(lua, "ctx.fetch: invalid array element type");
      }
      break;
    }

    default: return luaL_error(lua, "ctx.fetch: argument must be string or table");
  }

  // Build requests with collision handling
  std::vector<fetch_request> requests;
  for (size_t idx = 0; idx < urls.size(); ++idx) {
    auto const &url{ urls[idx] };
    auto const &ref{ refs[idx] };
    std::string basename{ uri_extract_filename(url) };
    if (basename.empty()) {
      return luaL_error(lua,
                        "ctx.fetch: cannot extract filename from URL: %s",
                        url.c_str());
    }

    // Handle collisions: append -2, -3, etc.
    std::string final_basename{ basename };
    int suffix{ 2 };
    while (ctx->used_basenames.contains(final_basename)) {
      size_t const dot_pos{ basename.find_last_of('.') };
      if (dot_pos != std::string::npos) {
        final_basename = basename.substr(0, dot_pos) + "-" + std::to_string(suffix) +
                         basename.substr(dot_pos);
      } else {
        final_basename = basename + "-" + std::to_string(suffix);
      }
      ++suffix;
    }
    ctx->used_basenames.insert(final_basename);
    basenames.push_back(final_basename);

    // Git repos go to stage_dir, everything else to run_dir (tmp)
    auto const info{ uri_classify(url) };
    std::filesystem::path dest{ info.scheme == uri_scheme::GIT
                                    ? ctx->stage_dir / final_basename
                                    : ctx->run_dir / final_basename };

    try {
      requests.push_back(url_to_fetch_request(url, dest, ref, "ctx.fetch"));
    } catch (std::exception const &e) {
      return luaL_error(lua, "ctx.fetch: %s", e.what());
    }
  }

  // Execute downloads (blocking, synchronous)
  tui::debug("ctx.fetch: downloading %zu file(s) to %s",
             urls.size(),
             ctx->run_dir.string().c_str());

  auto const start_time{ std::chrono::steady_clock::now() };

  if (tui::trace_enabled()) {
    std::string trace_url{ urls.empty() ? "" : urls[0] };
    if (urls.size() > 1) {
      trace_url += " (+" + std::to_string(urls.size() - 1) + " more)";
    }
    std::string const trace_dest{ urls.empty() ? "" : basenames[0] };
    ENVY_TRACE_LUA_CTX_FETCH_START(ctx->recipe_->spec->identity, trace_url, trace_dest);
  }

  auto const results{ fetch(requests) };
  auto const duration_ms{ std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - start_time)
                              .count() };

  if (tui::trace_enabled()) {
    std::string trace_url{ urls.empty() ? "" : urls[0] };
    if (urls.size() > 1) {
      trace_url += " (+" + std::to_string(urls.size() - 1) + " more)";
    }
    ENVY_TRACE_LUA_CTX_FETCH_COMPLETE(ctx->recipe_->spec->identity,
                                      trace_url,
                                      0,
                                      static_cast<std::int64_t>(duration_ms));
  }

  // Check for errors (no SHA256 verification here)
  std::vector<std::string> errors;
  for (size_t i = 0; i < results.size(); ++i) {
    if (auto const *err{ std::get_if<std::string>(&results[i]) }) {
      errors.push_back(urls[i] + ": " + *err);
    }
  }

  if (!errors.empty()) {
    std::ostringstream oss;
    oss << "ctx.fetch failed:\n";
    for (auto const &err : errors) { oss << "  " << err << "\n"; }
    return luaL_error(lua, "%s", oss.str().c_str());
  }

  // Return basename(s) to Lua
  if (is_array || urls.size() > 1) {
    lua_createtable(lua, static_cast<int>(basenames.size()), 0);
    for (size_t i = 0; i < basenames.size(); ++i) {
      lua_pushstring(lua, basenames[i].c_str());
      lua_rawseti(lua, -2, i + 1);
    }
  } else {
    lua_pushstring(lua, basenames[0].c_str());
  }

  return 1;
}

}  // namespace envy
