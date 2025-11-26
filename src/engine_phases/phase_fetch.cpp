#include "phase_fetch.h"

#include "cache.h"
#include "engine.h"
#include "extract.h"
#include "fetch.h"
#include "lua_ctx/lua_ctx_bindings.h"
#include "lua_envy.h"
#include "lua_shell.h"
#include "recipe.h"
#include "sha256.h"
#include "shell.h"
#include "trace.h"
#include "tui.h"
#include "uri.h"
#ifdef ENVY_FUNCTIONAL_TESTER
#include "test_support.h"
#endif

extern "C" {
#include "lua.h"
}

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace envy {

// Create fetch_request from URL and destination, validating scheme
// Not in anonymous namespace so tests can call it
fetch_request url_to_fetch_request(std::string const &url,
                                   std::filesystem::path const &dest,
                                   std::optional<std::string> const &ref,
                                   std::string const &context) {
  auto const info{ uri_classify(url) };

  switch (info.scheme) {
    case uri_scheme::HTTP: return fetch_request_http{ .source = url, .destination = dest };
    case uri_scheme::HTTPS:
      return fetch_request_https{ .source = url, .destination = dest };
    case uri_scheme::FTP: return fetch_request_ftp{ .source = url, .destination = dest };
    case uri_scheme::FTPS: return fetch_request_ftps{ .source = url, .destination = dest };
    case uri_scheme::S3:
      return fetch_request_s3{ .source = url, .destination = dest };
      // TODO: region support
    case uri_scheme::LOCAL_FILE_ABSOLUTE:
    case uri_scheme::LOCAL_FILE_RELATIVE:
      return fetch_request_file{ .source = url, .destination = dest };
      // TODO: file_root support
    case uri_scheme::GIT:
      if (!ref.has_value() || ref->empty()) {
        throw std::runtime_error("Git URLs require 'ref' field in " + context);
      }
      return fetch_request_git{ .source = url, .destination = dest, .ref = *ref };
    default: throw std::runtime_error("Unsupported URL scheme in " + context + ": " + url);
  }
}

namespace {

// Helper to extract destination from fetch_request variant
std::filesystem::path const &get_destination(fetch_request const &req) {
  return std::visit(
      [](auto const &r) -> std::filesystem::path const & { return r.destination; },
      req);
}

// Helper to extract source from fetch_request variant
std::string const &get_source(fetch_request const &req) {
  return std::visit([](auto const &r) -> std::string const & { return r.source; }, req);
}

struct fetch_spec {
  fetch_request request;
  std::string sha256;
};

struct table_entry {
  std::string url;
  std::string sha256;
  std::optional<std::string> ref;
};

std::vector<fetch_spec> parse_fetch_field(lua_State *lua,
                                          std::filesystem::path const &fetch_dir,
                                          std::filesystem::path const &stage_dir,
                                          std::string const &key);
std::vector<size_t> determine_downloads_needed(std::vector<fetch_spec> const &specs);
void execute_downloads(std::vector<fetch_spec> const &specs,
                       std::vector<size_t> const &to_download_indices,
                       std::string const &key);

sol::table build_fetch_phase_ctx_table(lua_State *lua,
                                       std::string const &identity,
                                       fetch_phase_ctx *ctx) {
  sol::state_view lua_view{ lua };
  sol::table ctx_table{ lua_view.create_table() };

  ctx_table["identity"] = identity;

  ctx_table["tmp"] = ctx->run_dir.string();

  // ctx.fetch - downloads files from URLs
  ctx_table["fetch"] = [ctx, lua](sol::object arg) -> sol::object {
    sol::state_view lua_view{ lua };
    std::vector<std::string> urls;
    std::vector<std::optional<std::string>> refs;
    std::vector<std::string> basenames;
    bool is_array{ false };

    // Parse argument: string, string array, table, or table array
    if (arg.is<std::string>()) {
      urls.push_back(arg.as<std::string>());
      refs.push_back(std::nullopt);
    } else if (arg.is<sol::table>()) {
      sol::table tbl{ arg.as<sol::table>() };
      sol::optional<sol::object> first_elem{ tbl[1] };

      if (!first_elem || first_elem->get_type() == sol::type::lua_nil) {
        // Single table {source="...", ref="..."}
        sol::optional<std::string> source{ tbl["source"] };
        if (!source) {
          throw std::runtime_error("ctx.fetch: table missing 'source' field");
        }
        urls.push_back(*source);

        sol::optional<std::string> ref{ tbl["ref"] };
        refs.push_back(ref ? std::optional<std::string>{ *ref } : std::nullopt);
      } else if (first_elem->is<std::string>()) {
        // Array of strings {"url1", "url2"}
        is_array = true;
        for (size_t i = 1;; ++i) {
          sol::optional<std::string> elem{ tbl[i] };
          if (!elem) break;
          urls.push_back(*elem);
          refs.push_back(std::nullopt);
        }
      } else if (first_elem->is<sol::table>()) {
        // Array of tables {{source="...", ref="..."}, {...}}
        is_array = true;
        for (size_t i = 1;; ++i) {
          sol::optional<sol::table> elem{ tbl[i] };
          if (!elem) break;

          sol::optional<std::string> source{ (*elem)["source"] };
          if (!source) {
            throw std::runtime_error("ctx.fetch: array element " + std::to_string(i) +
                                     " missing 'source' field");
          }
          urls.push_back(*source);

          sol::optional<std::string> ref{ (*elem)["ref"] };
          refs.push_back(ref ? std::optional<std::string>{ *ref } : std::nullopt);
        }
      } else {
        throw std::runtime_error("ctx.fetch: invalid array element type");
      }
    } else {
      throw std::runtime_error("ctx.fetch: argument must be string or table");
    }

    // Build requests with collision handling
    std::vector<fetch_request> requests;
    for (size_t idx = 0; idx < urls.size(); ++idx) {
      auto const &url{ urls[idx] };
      auto const &ref{ refs[idx] };
      std::string basename{ uri_extract_filename(url) };
      if (basename.empty()) {
        throw std::runtime_error("ctx.fetch: cannot extract filename from URL: " + url);
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
        throw std::runtime_error(std::string("ctx.fetch: ") + e.what());
      }
    }

    // Execute downloads
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

    // Check for errors
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
      sol::table result{ lua_view.create_table() };
      for (size_t i = 0; i < basenames.size(); ++i) { result[i + 1] = basenames[i]; }
      return result;
    } else {
      return sol::make_object(lua, basenames[0]);
    }
  };

  // ctx.commit_fetch - verifies SHA256 and moves files from tmp to fetch_dir
  ctx_table["commit_fetch"] = [ctx, lua](sol::object arg) {
    struct commit_entry {
      std::string filename;
      std::string sha256;
    };

    std::vector<commit_entry> entries;

    // Parse argument: string, string array, table, or table array
    if (arg.is<std::string>()) {
      entries.push_back({ arg.as<std::string>(), "" });
    } else if (arg.is<sol::table>()) {
      sol::table tbl{ arg.as<sol::table>() };
      sol::optional<sol::object> first_elem{ tbl[1] };

      if (!first_elem || first_elem->get_type() == sol::type::lua_nil) {
        // Single table {filename="...", sha256="..."}
        sol::optional<std::string> filename{ tbl["filename"] };
        if (!filename) {
          throw std::runtime_error("ctx.commit_fetch: table missing 'filename' field");
        }
        sol::optional<std::string> sha256_str{ tbl["sha256"] };
        entries.push_back({ *filename, sha256_str.value_or("") });
      } else if (first_elem->is<std::string>()) {
        // Array of strings {"file1", "file2"}
        for (size_t i = 1;; ++i) {
          sol::optional<std::string> elem{ tbl[i] };
          if (!elem) break;
          entries.push_back({ *elem, "" });
        }
      } else if (first_elem->is<sol::table>()) {
        // Array of tables {{filename="...", sha256="..."}, {...}}
        for (size_t i = 1;; ++i) {
          sol::optional<sol::table> elem{ tbl[i] };
          if (!elem) break;

          sol::optional<std::string> filename{ (*elem)["filename"] };
          if (!filename) {
            throw std::runtime_error("ctx.commit_fetch: array element " +
                                     std::to_string(i) + " missing 'filename' field");
          }
          sol::optional<std::string> sha256_str{ (*elem)["sha256"] };
          entries.push_back({ *filename, sha256_str.value_or("") });
        }
      } else {
        throw std::runtime_error("ctx.commit_fetch: invalid array element type");
      }
    } else {
      throw std::runtime_error("ctx.commit_fetch: argument must be string or table");
    }

    // Verify and move files
    std::vector<std::string> errors;
    for (auto const &entry : entries) {
      std::filesystem::path const src{ ctx->run_dir / entry.filename };
      std::filesystem::path const dest{ ctx->fetch_dir / entry.filename };

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
  };

  // Add common context bindings (copy, move, extract, extract_all, asset, ls, run)
  lua_ctx_add_common_bindings(ctx_table, ctx);
  return ctx_table;
}

bool run_programmatic_fetch(sol::protected_function fetch_func,
                            cache::scoped_entry_lock *lock,
                            std::string const &identity,
                            engine &eng,
                            recipe *r) {
  tui::debug("phase fetch: executing fetch function");

  // Create temp workspace for ctx.tmp
  std::filesystem::path const tmp_dir{ lock->work_dir() / "tmp" };
  std::filesystem::create_directories(tmp_dir);

  // Ensure stage_dir exists (needed for git repos)
  std::error_code ec;
  std::filesystem::create_directories(lock->stage_dir(), ec);
  if (ec) {
    throw std::runtime_error("Failed to create stage directory: " + ec.message());
  }

  // Build context (inherits from lua_ctx_common)
  fetch_phase_ctx ctx{};
  ctx.fetch_dir = lock->fetch_dir();
  ctx.run_dir = tmp_dir;
  ctx.stage_dir = lock->stage_dir();
  ctx.engine_ = &eng;
  ctx.recipe_ = r;
  ctx.used_basenames = {};

  lua_State *L{ fetch_func.lua_state() };
  sol::table ctx_table{ build_fetch_phase_ctx_table(L, identity, &ctx) };

  // Get options from registry and pass as 2nd arg
  lua_rawgeti(L, LUA_REGISTRYINDEX, ENVY_OPTIONS_RIDX);
  sol::object opts{ sol::stack_object{ L, -1 } };
  lua_pop(L, 1);  // Pop options from stack (opts now owns a reference)

  sol::protected_function_result result{ fetch_func(ctx_table, opts) };

  if (!result.valid()) {
    sol::error err{ result };
    throw std::runtime_error("Fetch function failed for " + identity + ": " + err.what());
  }

  bool should_mark_complete{ true };
  lua_State *lua{ fetch_func.lua_state() };
  sol::object return_value{ result };

  if (return_value.get_type() == sol::type::none ||
      return_value.get_type() == sol::type::lua_nil) {
    tui::debug("phase fetch: function returned nil, imperative mode only");
  } else if (return_value.is<std::string>() || return_value.is<sol::table>()) {
    tui::debug("phase fetch: function returned declarative spec, processing");

    return_value.push(lua);
    auto const fetch_specs{
      parse_fetch_field(lua, lock->fetch_dir(), lock->stage_dir(), identity)
    };
    lua_pop(lua, 1);

    if (!fetch_specs.empty()) {
      execute_downloads(fetch_specs, determine_downloads_needed(fetch_specs), identity);

      bool const has_git_repos =
          std::any_of(fetch_specs.begin(), fetch_specs.end(), [](auto const &spec) {
            return std::holds_alternative<fetch_request_git>(spec.request);
          });

      if (has_git_repos) {
        tui::debug("phase fetch: returned spec contains git repos, not cacheable");
        should_mark_complete = false;
      }
    }
  } else {
    throw std::runtime_error("Fetch function for " + identity +
                             " must return nil, string, or table (got " +
                             sol::type_name(lua, return_value.get_type()) + ")");
  }

  std::filesystem::remove_all(tmp_dir);
  return should_mark_complete;
}

// Extract source, sha256, and ref from a Lua table at the top of the stack.
table_entry parse_table_entry(lua_State *lua, std::string const &context) {
  if (!lua_istable(lua, -1)) { throw std::runtime_error("Expected table in " + context); }

  lua_getfield(lua, -1, "source");
  if (!lua_isstring(lua, -1)) {
    lua_pop(lua, 1);
    throw std::runtime_error("Fetch table missing 'source' field in " + context);
  }
  std::string url{ lua_tostring(lua, -1) };
  lua_pop(lua, 1);

  lua_getfield(lua, -1, "sha256");
  std::string sha256;
  if (lua_isstring(lua, -1)) { sha256 = lua_tostring(lua, -1); }
  lua_pop(lua, 1);

  lua_getfield(lua, -1, "ref");
  std::optional<std::string> ref;
  if (lua_isstring(lua, -1)) { ref = lua_tostring(lua, -1); }
  lua_pop(lua, 1);

  return { std::move(url), std::move(sha256), std::move(ref) };
}

// Create fetch_spec from URL, SHA256, and optional ref, checking for filename collisions.
fetch_spec create_fetch_spec(std::string url,
                             std::string sha256,
                             std::optional<std::string> ref,
                             std::filesystem::path const &fetch_dir,
                             std::filesystem::path const &stage_dir,
                             std::unordered_set<std::string> &basenames,
                             std::string const &context) {
  std::string basename{ uri_extract_filename(url) };
  if (basename.empty()) {
    throw std::runtime_error("Cannot extract filename from URL: " + url + " in " +
                             context);
  }

  if (basenames.contains(basename)) {
    throw std::runtime_error("Fetch filename collision: " + basename + " in " + context);
  }
  basenames.insert(basename);

  auto const info{ uri_classify(url) };

  // Git repos go directly to stage_dir (no extraction needed), everything else to
  // fetch_dir
  std::filesystem::path const dest{ info.scheme == uri_scheme::GIT
                                        ? stage_dir / basename
                                        : fetch_dir / basename };

  return { .request = url_to_fetch_request(url, dest, ref, context),
           .sha256 = std::move(sha256) };
}

// Parse the fetch field from Lua into a vector of fetch_specs.
std::vector<fetch_spec> parse_fetch_field(lua_State *lua,
                                          std::filesystem::path const &fetch_dir,
                                          std::filesystem::path const &stage_dir,
                                          std::string const &key) {
  int const fetch_type{ lua_type(lua, -1) };

  switch (fetch_type) {
    case LUA_TSTRING: {
      char const *url{ lua_tostring(lua, -1) };
      std::string basename{ uri_extract_filename(url) };
      if (basename.empty()) {
        throw std::runtime_error("Cannot extract filename from URL: " + std::string(url) +
                                 " in " + key);
      }
      std::filesystem::path dest{ fetch_dir / basename };

      return { { .request = url_to_fetch_request(url, dest, std::nullopt, key),
                 .sha256 = "" } };
    }

    case LUA_TTABLE: {
      std::vector<fetch_spec> specs;
      std::unordered_set<std::string> basenames;

      sol::state_view lua_view{ lua };
      sol::table tbl{ lua_view, -1 };

      sol::object first_elem{ tbl[1] };
      sol::type first_elem_type{ first_elem.get_type() };

      auto process_table_entry{ [&]() {
        auto entry{ parse_table_entry(lua, key) };
        specs.push_back(create_fetch_spec(std::move(entry.url),
                                          std::move(entry.sha256),
                                          std::move(entry.ref),
                                          fetch_dir,
                                          stage_dir,
                                          basenames,
                                          key));
      } };

      if (first_elem_type == sol::type::none || first_elem_type == sol::type::lua_nil) {
        process_table_entry();
      } else if (first_elem_type == sol::type::string) {
        size_t const len{ tbl.size() };
        for (size_t i = 1; i <= len; ++i) {
          sol::object elem{ tbl[i] };
          if (!elem.is<std::string>()) {
            throw std::runtime_error("Array element " + std::to_string(i) +
                                     " must be string in " + key);
          }
          std::string url{ elem.as<std::string>() };

          specs.push_back(create_fetch_spec(std::move(url),
                                            "",
                                            std::nullopt,
                                            fetch_dir,
                                            stage_dir,
                                            basenames,
                                            key));
        }
      } else if (first_elem_type == sol::type::table) {
        size_t const len{ tbl.size() };
        for (size_t i = 1; i <= len; ++i) {
          sol::table elem{ tbl[i] };
          elem.push(lua);
          process_table_entry();
          lua_pop(lua, 1);
        }
      } else {
        throw std::runtime_error("Invalid fetch array element type in " + key);
      }

      return specs;
    }

    default:
      throw std::runtime_error("Fetch field must be string, table, or function in " + key);
  }
}

// Check cache and determine which files need downloading.
std::vector<size_t> determine_downloads_needed(std::vector<fetch_spec> const &specs) {
  std::vector<size_t> to_download;

  for (size_t i = 0; i < specs.size(); ++i) {
    auto const &spec{ specs[i] };
    auto const &dest{ get_destination(spec.request) };

    if (!std::filesystem::exists(dest)) {  // File doesn't exist: download
      to_download.push_back(i);
      continue;
    }

    if (spec.sha256.empty()) {  // No SHA256: always re-download (no cache trust)
      tui::debug("phase fetch: no SHA256 for %s, re-downloading (no cache)",
                 dest.filename().string().c_str());
      std::filesystem::remove(dest);
      to_download.push_back(i);
      continue;
    }

    // File exists with SHA256 - verify cached version
    try {
      tui::debug("phase fetch: verifying cached file %s", dest.string().c_str());
      sha256_verify(spec.sha256, sha256(dest));
      tui::debug("phase fetch: cache hit for %s", dest.filename().string().c_str());
    } catch (std::exception const &e) {  // hash mismatch, delete and re-download
      tui::debug("phase fetch: cache mismatch for %s, deleting", dest.string().c_str());
      std::filesystem::remove(dest);
      to_download.push_back(i);
    }
  }

  return to_download;
}

// Execute downloads and verification for specs that need downloading.
void execute_downloads(std::vector<fetch_spec> const &specs,
                       std::vector<size_t> const &to_download_indices,
                       std::string const &key) {
  if (to_download_indices.empty()) {
    tui::debug("phase fetch: all files cached, no downloads needed");
    return;
  }

  tui::debug("phase fetch: downloading %zu file(s)", to_download_indices.size());

  std::vector<fetch_request> requests;
  requests.reserve(to_download_indices.size());
  for (auto idx : to_download_indices) { requests.push_back(specs[idx].request); }

  // TODO: Add FETCH_FILE_START/COMPLETE trace events (requires passing recipe identity)

  auto const results{ fetch(requests) };

  std::vector<std::string> errors;
  for (size_t i = 0; i < results.size(); ++i) {
    auto const spec_idx{ to_download_indices[i] };
    std::string url{ get_source(specs[spec_idx].request) };

    if (auto const *err{ std::get_if<std::string>(&results[i]) }) {
      errors.push_back(url + ": " + *err);
    } else {
      // File downloaded successfully
      auto const *result{ std::get_if<fetch_result>(&results[i]) };
      if (result) {
        tui::debug("phase fetch: downloaded %s",
                   result->resolved_destination.filename().string().c_str());
      }

#ifdef ENVY_FUNCTIONAL_TESTER
      try {
        test::decrement_fail_counter();
      } catch (std::exception const &e) {
        errors.push_back(get_source(specs[spec_idx].request) + ": " + e.what());
        continue;
      }
#endif

      if (!specs[spec_idx].sha256.empty()) {
        try {
          auto const *result{ std::get_if<fetch_result>(&results[i]) };
          if (!result) { throw std::runtime_error("Unexpected result type"); }
          tui::debug("phase fetch: verifying SHA256 for %s",
                     result->resolved_destination.string().c_str());
          sha256_verify(specs[spec_idx].sha256, sha256(result->resolved_destination));
        } catch (std::exception const &e) {
          errors.push_back(get_source(specs[spec_idx].request) + ": " + e.what());
        }
      }
    }
  }

  if (!errors.empty()) {
    std::ostringstream oss;
    oss << "Fetch failed for " << key << ":\n";
    for (auto const &err : errors) { oss << "  " << err << "\n"; }
    throw std::runtime_error(oss.str());
  }
}

// fetch = "source" or fetch = {source="..."} or fetch = {{...}}
// Returns true if fetch should be marked complete (cacheable), false otherwise
bool run_declarative_fetch(lua_State *lua,
                           cache::scoped_entry_lock *lock,
                           std::string const &identity) {
  tui::debug("phase fetch: executing declarative fetch");

  // Ensure stage_dir exists (needed for git repos that clone directly there)
  std::error_code ec;
  std::filesystem::create_directories(lock->stage_dir(), ec);
  if (ec) {
    throw std::runtime_error("Failed to create stage directory: " + ec.message());
  }

  auto const fetch_specs{
    parse_fetch_field(lua, lock->fetch_dir(), lock->stage_dir(), identity)
  };
  lua_pop(lua, 1);
  if (fetch_specs.empty()) { return true; }  // No specs = cacheable (nothing to do)
  execute_downloads(fetch_specs, determine_downloads_needed(fetch_specs), identity);

  // Check if we fetched any git repos - if so, don't mark fetch complete (git clones are
  // not cacheable)
  bool const has_git_repos =
      std::any_of(fetch_specs.begin(), fetch_specs.end(), [](auto const &spec) {
        return std::holds_alternative<fetch_request_git>(spec.request);
      });

  if (has_git_repos) {
    tui::debug(
        "phase fetch: skipping fetch completion marker (git repos are not cacheable)");
    return false;  // Don't mark complete
  }

  return true;  // Mark complete
}

}  // namespace

void run_fetch_phase(recipe *r, engine &eng) {
  phase_trace_scope const phase_scope{ r->spec->identity,
                                       recipe_phase::asset_fetch,
                                       std::chrono::steady_clock::now() };

  cache::scoped_entry_lock *lock = r->lock.get();
  if (!lock) {
    tui::debug("phase fetch: no lock (cache hit), skipping");
    return;
  }

  if (lock->is_fetch_complete()) {
    tui::debug("phase fetch: fetch already complete, skipping");
    return;
  }

  std::string const &identity{ r->spec->identity };

  lua_State *lua{ r->lua->lua_state() };
  sol::state_view lua_view{ lua };
  sol::object fetch_obj{ lua_view["fetch"] };

  bool should_mark_complete{ true };

  if (!fetch_obj.valid()) {
    tui::debug("phase fetch: no fetch field, skipping");
    return;
  } else if (fetch_obj.is<sol::protected_function>()) {
    should_mark_complete = run_programmatic_fetch(fetch_obj.as<sol::protected_function>(),
                                                  lock,
                                                  identity,
                                                  eng,
                                                  r);
  } else if (fetch_obj.is<std::string>() || fetch_obj.is<sol::table>()) {
    fetch_obj.push(lua);
    should_mark_complete = run_declarative_fetch(lua, lock, identity);
  } else {
    throw std::runtime_error("Fetch field must be nil, string, table, or function in " +
                             identity);
  }

  if (should_mark_complete) {
    lock->mark_fetch_complete();
    tui::debug("phase fetch: marked fetch complete");
  }
}

}  // namespace envy
