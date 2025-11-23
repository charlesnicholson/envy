#include "phase_fetch.h"

#include "cache.h"
#include "engine.h"
#include "recipe.h"
#include "fetch.h"
#include "lua_ctx/lua_ctx_bindings.h"
#include "lua_util.h"
#include "sha256.h"
#include "tui.h"
#include "uri.h"
#ifdef ENVY_FUNCTIONAL_TESTER
#include "test_support.h"
#endif

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

#include <algorithm>
#include <filesystem>
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

// Build context table for fetch function
void build_fetch_phase_ctx_table(lua_State *lua,
                               std::string const &identity,
                               std::unordered_map<std::string, lua_value> const &options,
                               fetch_phase_ctx *ctx) {
  lua_createtable(lua, 0, 10);  // Pre-allocate space for 10 fields

  // ctx.identity
  lua_pushstring(lua, identity.c_str());
  lua_setfield(lua, -2, "identity");

  // ctx.options (always present, even if empty)
  lua_createtable(lua, 0, static_cast<int>(options.size()));
  for (auto const &[key, val] : options) {
    value_to_lua_stack(lua, val);
    lua_setfield(lua, -2, key.c_str());
  }
  lua_setfield(lua, -2, "options");

  // ctx.tmp
  lua_pushstring(lua, ctx->run_dir.string().c_str());
  lua_setfield(lua, -2, "tmp");

  // Fetch-phase bindings (ctx.fetch + ctx.commit_fetch)
  lua_ctx_bindings_register_fetch_phase(lua, ctx);

  // Common context bindings (all phases)
  lua_ctx_bindings_register_run(lua, ctx);
  lua_ctx_bindings_register_asset(lua, ctx);
  lua_ctx_bindings_register_copy(lua, ctx);
  lua_ctx_bindings_register_move(lua, ctx);
  lua_ctx_bindings_register_extract(lua, ctx);
  lua_ctx_bindings_register_ls(lua, ctx);
}

// fetch = function(ctx) ... end
// Returns true if fetch should be marked complete (cacheable), false otherwise
bool run_programmatic_fetch(lua_State *lua,
                            cache::scoped_entry_lock *lock,
                            std::string const &identity,
                            std::unordered_map<std::string, lua_value> const &options,
                            engine &eng,
                            recipe *r) {
  tui::trace("phase fetch: executing fetch function");

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

  build_fetch_phase_ctx_table(lua, identity, options, &ctx);

  // Call fetch(ctx)
  // Stack: fetch function at -2, context table at -1 (ready for pcall)
  if (lua_pcall(lua, 1, 1, 0) != LUA_OK) {
    char const *err{ lua_tostring(lua, -1) };
    std::string error_msg{ err ? err : "unknown error" };
    lua_pop(lua, 1);
    throw std::runtime_error("Fetch function failed for " + identity + ": " + error_msg);
  }

  // Check return value: nil (imperative only) or string/table (declarative)
  int const return_type{ lua_type(lua, -1) };
  bool should_mark_complete{ true };

  switch (return_type) {
    case LUA_TNIL:
    case LUA_TNONE:
      // Imperative only - no declarative fetch to process
      tui::trace("phase fetch: function returned nil, imperative mode only");
      lua_pop(lua, 1);
      break;

    case LUA_TSTRING:
    case LUA_TTABLE: {
      // Declarative return - reuse existing declarative fetch machinery
      tui::trace("phase fetch: function returned declarative spec, processing");

      // Parse and execute declarative fetch from return value
      auto const fetch_specs{
        parse_fetch_field(lua, lock->fetch_dir(), lock->stage_dir(), identity)
      };
      lua_pop(lua, 1);

      if (!fetch_specs.empty()) {
        execute_downloads(fetch_specs, determine_downloads_needed(fetch_specs), identity);

        // Check if we fetched any git repos - if so, don't mark fetch complete
        bool const has_git_repos =
            std::any_of(fetch_specs.begin(), fetch_specs.end(), [](auto const &spec) {
              return std::holds_alternative<fetch_request_git>(spec.request);
            });

        if (has_git_repos) {
          tui::trace("phase fetch: returned spec contains git repos, not cacheable");
          should_mark_complete = false;
        }
      }
      break;
    }

    default:
      lua_pop(lua, 1);
      throw std::runtime_error("Fetch function for " + identity +
                               " must return nil, string, or table (got " +
                               lua_typename(lua, return_type) + ")");
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

      // Detect array type: check first element
      lua_rawgeti(lua, -1, 1);
      int const first_elem_type{ lua_type(lua, -1) };
      lua_pop(lua, 1);

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

      switch (first_elem_type) {
        case LUA_TNIL:  // Single table {url="...", sha256="..."}
          process_table_entry();
          break;

        case LUA_TSTRING: {  // Array of strings {"url1", "url2", ...}
          size_t const len{ lua_rawlen(lua, -1) };
          for (size_t i = 1; i <= len; ++i) {
            lua_rawgeti(lua, -1, i);
            if (!lua_isstring(lua, -1)) {
              throw std::runtime_error("Array element " + std::to_string(i) +
                                       " must be string in " + key);
            }
            std::string url{ lua_tostring(lua, -1) };
            lua_pop(lua, 1);

            specs.push_back(create_fetch_spec(std::move(url),
                                              "",
                                              std::nullopt,
                                              fetch_dir,
                                              stage_dir,
                                              basenames,
                                              key));
          }
          break;
        }

        case LUA_TTABLE: {  // Array of tables {{url="...", sha256="..."}, {...}}
          size_t const len{ lua_rawlen(lua, -1) };
          for (size_t i = 1; i <= len; ++i) {
            lua_rawgeti(lua, -1, i);
            process_table_entry();
            lua_pop(lua, 1);
          }
          break;
        }

        default: throw std::runtime_error("Invalid fetch array element type in " + key);
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
      tui::trace("phase fetch: no SHA256 for %s, re-downloading (no cache)",
                 dest.filename().string().c_str());
      std::filesystem::remove(dest);
      to_download.push_back(i);
      continue;
    }

    // File exists with SHA256 - verify cached version
    try {
      tui::trace("phase fetch: verifying cached file %s", dest.string().c_str());
      sha256_verify(spec.sha256, sha256(dest));
      tui::trace("phase fetch: cache hit for %s", dest.filename().string().c_str());
    } catch (std::exception const &e) {  // hash mismatch, delete and re-download
      tui::trace("phase fetch: cache mismatch for %s, deleting", dest.string().c_str());
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
    tui::trace("phase fetch: all files cached, no downloads needed");
    return;
  }

  tui::trace("phase fetch: downloading %zu file(s)", to_download_indices.size());

  std::vector<fetch_request> requests;
  requests.reserve(to_download_indices.size());
  for (auto idx : to_download_indices) { requests.push_back(specs[idx].request); }

  auto const results{ fetch(requests) };

  std::vector<std::string> errors;
  for (size_t i = 0; i < results.size(); ++i) {
    auto const spec_idx{ to_download_indices[i] };
    if (auto const *err{ std::get_if<std::string>(&results[i]) }) {
      errors.push_back(get_source(specs[spec_idx].request) + ": " + *err);
    } else {
      // File downloaded successfully
      auto const *result{ std::get_if<fetch_result>(&results[i]) };
      if (result) {
        tui::trace("phase fetch: downloaded %s",
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
          tui::trace("phase fetch: verifying SHA256 for %s",
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
  tui::trace("phase fetch: executing declarative fetch");

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
    tui::trace(
        "phase fetch: skipping fetch completion marker (git repos are not cacheable)");
    return false;  // Don't mark complete
  }

  return true;  // Mark complete
}

}  // namespace

void run_fetch_phase(recipe *r, engine &eng) {
  std::string const key{ r->spec.format_key() };
  tui::trace("phase fetch START [%s]", key.c_str());

  cache::scoped_entry_lock *lock = r->lock.get();
  if (!lock) {
    tui::trace("phase fetch: no lock (cache hit), skipping");
    return;
  }

  if (lock->is_fetch_complete()) {
    tui::trace("phase fetch: fetch already complete, skipping");
    return;
  }

  std::string const &identity{ r->spec.identity };
  std::unordered_map<std::string, lua_value> const &options{ r->spec.options };

  lua_State *lua{ r->lua_state.get() };
  lua_getglobal(lua, "fetch");
  int const fetch_type{ lua_type(lua, -1) };

  bool should_mark_complete{ true };

  switch (fetch_type) {
    case LUA_TNIL:
      lua_pop(lua, 1);
      tui::trace("phase fetch: no fetch field, skipping");
      return;
    case LUA_TFUNCTION:
      should_mark_complete =
          run_programmatic_fetch(lua, lock, identity, options, eng, r);
      break;
    case LUA_TSTRING:
    case LUA_TTABLE:
      should_mark_complete = run_declarative_fetch(lua, lock, identity);
      break;
    default:
      lua_pop(lua, 1);
      throw std::runtime_error("Fetch field must be nil, string, table, or function in " +
                               identity);
  }

  if (should_mark_complete) {
    lock->mark_fetch_complete();
    tui::trace("phase fetch: marked fetch complete");
  }
}

}  // namespace envy
