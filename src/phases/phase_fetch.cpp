#include "phase_fetch.h"

#include "cache.h"
#include "engine.h"
#include "fetch.h"
#include "lua_ctx/lua_ctx_bindings.h"
#include "lua_envy.h"
#include "lua_error_formatter.h"
#include "recipe.h"
#include "sha256.h"
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
#include <filesystem>
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

std::vector<fetch_spec> parse_fetch_field(sol::state_view lua,
                                          std::filesystem::path const &fetch_dir,
                                          std::filesystem::path const &stage_dir,
                                          std::string const &key);
std::vector<size_t> determine_downloads_needed(std::vector<fetch_spec> const &specs);
void execute_downloads(std::vector<fetch_spec> const &specs,
                       std::vector<size_t> const &to_download_indices,
                       std::string const &key);

bool run_programmatic_fetch(sol::protected_function fetch_func,
                            cache::scoped_entry_lock *lock,
                            std::string const &identity,
                            engine &eng,
                            recipe *r) {
  tui::debug("phase fetch: executing fetch function");

  // Create temp workspace for ctx.tmp_dir
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

  sol::state_view lua{ fetch_func.lua_state() };
  sol::table ctx_table{ build_fetch_phase_ctx_table(lua, identity, &ctx) };

  // Get options from registry and pass as 2nd arg
  sol::object opts{ lua.registry()[ENVY_OPTIONS_RIDX] };

  sol::protected_function_result result{ call_lua_function_with_enriched_errors(
      r,
      "fetch",
      [&]() { return fetch_func(ctx_table, opts); }) };

  bool should_mark_complete{ true };
  sol::object return_value{ result };

  if (return_value.get_type() == sol::type::none ||
      return_value.get_type() == sol::type::lua_nil) {
    tui::debug("phase fetch: function returned nil, imperative mode only");
  } else if (return_value.is<std::string>() || return_value.is<sol::table>()) {
    tui::debug("phase fetch: function returned declarative spec, processing");

    return_value.push(lua.lua_state());
    auto const fetch_specs{
      parse_fetch_field(lua, lock->fetch_dir(), lock->stage_dir(), identity)
    };
    lua_pop(lua.lua_state(), 1);

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

// Extract source, sha256, and ref from a Lua table.
table_entry parse_table_entry(sol::table const &tbl, std::string const &context) {
  sol::optional<std::string> url{ tbl["source"] };
  if (!url || url->empty()) {
    throw std::runtime_error("Fetch table missing 'source' field in " + context);
  }

  table_entry entry;
  entry.url = std::move(*url);

  sol::optional<std::string> sha{ tbl["sha256"] };
  if (sha) { entry.sha256 = std::move(*sha); }

  sol::optional<std::string> ref{ tbl["ref"] };
  if (ref) { entry.ref = std::move(*ref); }

  return entry;
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
std::vector<fetch_spec> parse_fetch_field(sol::state_view lua,
                                          std::filesystem::path const &fetch_dir,
                                          std::filesystem::path const &stage_dir,
                                          std::string const &key) {
  sol::stack_object top{ lua, -1 };
  sol::type const fetch_type{ top.get_type() };

  switch (fetch_type) {
    case sol::type::string: {
      std::string const url{ top.as<std::string>() };
      std::string basename{ uri_extract_filename(url) };
      if (basename.empty()) {
        throw std::runtime_error("Cannot extract filename from URL: " + url + " in " +
                                 key);
      }
      std::filesystem::path dest{ fetch_dir / basename };

      return { { .request = url_to_fetch_request(url, dest, std::nullopt, key),
                 .sha256 = "" } };
    }

    case sol::type::table: {
      std::vector<fetch_spec> specs;
      std::unordered_set<std::string> basenames;

      sol::table tbl{ lua, -1 };

      sol::object first_elem{ tbl[1] };
      sol::type first_elem_type{ first_elem.get_type() };

      auto process_table_entry{ [&](sol::table const &entry_tbl) {
        auto entry{ parse_table_entry(entry_tbl, key) };
        specs.push_back(create_fetch_spec(std::move(entry.url),
                                          std::move(entry.sha256),
                                          std::move(entry.ref),
                                          fetch_dir,
                                          stage_dir,
                                          basenames,
                                          key));
      } };

      if (first_elem_type == sol::type::none || first_elem_type == sol::type::lua_nil) {
        process_table_entry(tbl);
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
          process_table_entry(elem);
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
bool run_declarative_fetch(sol::state_view lua,
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
  lua_pop(lua.lua_state(), 1);
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

  sol::state_view lua_view{ *r->lua };
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
    fetch_obj.push(lua_view.lua_state());
    should_mark_complete = run_declarative_fetch(lua_view, lock, identity);
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
