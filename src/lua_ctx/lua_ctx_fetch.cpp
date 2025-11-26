#include "lua_ctx_bindings.h"

#include "fetch.h"
#include "recipe.h"
#include "trace.h"
#include "tui.h"
#include "uri.h"

#include <chrono>
#include <filesystem>
#include <functional>
#include <optional>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

namespace envy {

// Extern declaration (defined in phase_fetch.cpp)
fetch_request url_to_fetch_request(std::string const &url,
                                   std::filesystem::path const &dest,
                                   std::optional<std::string> const &ref,
                                   std::string const &context);

namespace {

struct fetch_item {
  std::string url;
  std::optional<std::string> ref;
};

// Parse ctx.fetch() arguments into a list of fetch items
std::pair<std::vector<fetch_item>, bool> parse_fetch_args(sol::object const &arg) {
  std::vector<fetch_item> items;
  bool is_array{ false };

  if (arg.is<std::string>()) {
    // Single string: "url"
    items.push_back({ arg.as<std::string>(), std::nullopt });
  } else if (arg.is<sol::table>()) {
    sol::table tbl{ arg.as<sol::table>() };

    // Check if it's an array or a single table
    sol::object first_elem{ tbl[1] };

    if (first_elem.get_type() == sol::type::lua_nil) {
      // Single table: {source="url", ref="branch"}
      sol::optional<std::string> source{ tbl["source"] };
      if (!source) { throw std::runtime_error("ctx.fetch: table missing 'source' field"); }
      sol::optional<std::string> ref{ tbl["ref"] };
      items.push_back(
          { *source, ref ? std::optional<std::string>{ *ref } : std::nullopt });
    } else if (first_elem.is<std::string>()) {
      // Array of strings: {"url1", "url2"}
      is_array = true;
      for (auto const &[key, value] : tbl) {
        if (!value.is<std::string>()) {
          throw std::runtime_error("ctx.fetch: array elements must be strings");
        }
        items.push_back({ value.as<std::string>(), std::nullopt });
      }
    } else if (first_elem.is<sol::table>()) {
      // Array of tables: {{source="url1", ref="..."}, {source="url2"}}
      is_array = true;
      for (auto const &[key, value] : tbl) {
        if (!value.is<sol::table>()) {
          throw std::runtime_error("ctx.fetch: array elements must be tables");
        }
        sol::table item_tbl{ value.as<sol::table>() };
        sol::optional<std::string> source{ item_tbl["source"] };
        if (!source) {
          throw std::runtime_error("ctx.fetch: array element missing 'source' field");
        }
        sol::optional<std::string> ref{ item_tbl["ref"] };
        items.push_back(
            { *source, ref ? std::optional<std::string>{ *ref } : std::nullopt });
      }
    } else {
      throw std::runtime_error("ctx.fetch: invalid array element type");
    }
  } else {
    throw std::runtime_error("ctx.fetch: argument must be string or table");
  }

  return { std::move(items), is_array };
}

}  // namespace

std::function<sol::object(sol::object, sol::this_state)> make_ctx_fetch(
    fetch_phase_ctx *ctx) {
  return [ctx](sol::object arg, sol::this_state L) -> sol::object {
    sol::state_view lua{ L };

    // Parse arguments
    auto [items, is_array] = parse_fetch_args(arg);

    std::vector<std::string> urls;
    std::vector<std::string> basenames;

    // Build requests with collision handling
    std::vector<fetch_request> requests;
    for (auto const &item : items) {
      std::string basename{ uri_extract_filename(item.url) };
      if (basename.empty()) {
        throw std::runtime_error("ctx.fetch: cannot extract filename from URL: " +
                                 item.url);
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
      urls.push_back(item.url);

      // Git repos go to stage_dir, everything else to run_dir (tmp)
      auto const info{ uri_classify(item.url) };
      std::filesystem::path dest{ info.scheme == uri_scheme::GIT
                                      ? ctx->stage_dir / final_basename
                                      : ctx->run_dir / final_basename };

      requests.push_back(url_to_fetch_request(item.url, dest, item.ref, "ctx.fetch"));
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
      throw std::runtime_error(oss.str());
    }

    // Return basename(s) to Lua
    if (is_array || urls.size() > 1) {
      sol::table result{ lua.create_table(static_cast<int>(basenames.size()), 0) };
      for (size_t i = 0; i < basenames.size(); ++i) { result[i + 1] = basenames[i]; }
      return result;
    } else {
      return sol::make_object(lua, basenames[0]);
    }
  };
}

}  // namespace envy
