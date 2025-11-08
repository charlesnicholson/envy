#include "phase_fetch.h"

#include "fetch.h"
#include "sha256.h"
#include "tui.h"
#ifdef ENVY_FUNCTIONAL_TESTER
#include "test_support.h"
#endif

#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace envy {

namespace {

struct fetch_spec {
  fetch_request request;
  std::string sha256;
};

struct table_entry {
  std::string url;
  std::string sha256;
};

// Extract url and sha256 from a Lua table at the top of the stack.
table_entry parse_table_entry(lua_State *lua, std::string const &context) {
  if (!lua_istable(lua, -1)) { throw std::runtime_error("Expected table in " + context); }

  lua_getfield(lua, -1, "url");
  if (!lua_isstring(lua, -1)) {
    lua_pop(lua, 1);
    throw std::runtime_error("Fetch table missing 'url' field in " + context);
  }
  std::string url{ lua_tostring(lua, -1) };
  lua_pop(lua, 1);

  lua_getfield(lua, -1, "sha256");
  std::string sha256;
  if (lua_isstring(lua, -1)) { sha256 = lua_tostring(lua, -1); }
  lua_pop(lua, 1);

  return { std::move(url), std::move(sha256) };
}

// Create fetch_spec from URL and SHA256, checking for filename collisions.
fetch_spec create_fetch_spec(std::string url,
                             std::string sha256,
                             std::filesystem::path const &fetch_dir,
                             std::unordered_set<std::string> &basenames,
                             std::string const &context) {
  std::filesystem::path dest{ fetch_dir / std::filesystem::path(url).filename() };
  std::string basename{ dest.filename().string() };

  if (basenames.contains(basename)) {
    throw std::runtime_error("Fetch filename collision: " + basename + " in " + context);
  }
  basenames.insert(basename);

  return { .request = { .source = std::move(url), .destination = std::move(dest) },
           .sha256 = std::move(sha256) };
}

// Parse the fetch field from Lua into a vector of fetch_specs.
std::vector<fetch_spec> parse_fetch_field(lua_State *lua,
                                          std::filesystem::path const &fetch_dir,
                                          std::string const &key) {
  std::vector<fetch_spec> specs;
  std::unordered_set<std::string> basenames;

  int const fetch_type{ lua_type(lua, -1) };

  if (fetch_type == LUA_TSTRING) {
    // Single URL string
    char const *url{ lua_tostring(lua, -1) };
    specs.push_back(create_fetch_spec(url, "", fetch_dir, basenames, key));
  } else if (fetch_type == LUA_TTABLE) {
    lua_rawgeti(lua, -1, 1);
    bool const is_array{ !lua_isnil(lua, -1) };
    lua_pop(lua, 1);

    if (is_array) {
      // Array of tables
      size_t const len{ lua_rawlen(lua, -1) };
      for (size_t i = 1; i <= len; ++i) {
        lua_rawgeti(lua, -1, i);
        auto entry{ parse_table_entry(lua, key) };
        specs.push_back(create_fetch_spec(std::move(entry.url),
                                          std::move(entry.sha256),
                                          fetch_dir,
                                          basenames,
                                          key));
        lua_pop(lua, 1);
      }
    } else {
      // Single table
      auto entry{ parse_table_entry(lua, key) };
      specs.push_back(create_fetch_spec(std::move(entry.url),
                                        std::move(entry.sha256),
                                        fetch_dir,
                                        basenames,
                                        key));
    }
  } else {
    throw std::runtime_error("Fetch field must be string, table, or function in " + key);
  }

  return specs;
}

// Check cache and determine which files need downloading.
std::vector<size_t> determine_downloads_needed(std::vector<fetch_spec> const &specs) {
  std::vector<size_t> to_download;

  for (size_t i = 0; i < specs.size(); ++i) {
    auto const &spec{ specs[i] };
    auto const &dest{ spec.request.destination };

    if (std::filesystem::exists(dest)) {
      if (!spec.sha256.empty()) {
        // SHA256 provided: verify cached file
        try {
          tui::trace("phase fetch: verifying cached file %s", dest.string().c_str());
          sha256_verify(spec.sha256, sha256(dest));
          tui::trace("phase fetch: cache hit for %s", dest.filename().string().c_str());
          // Cache hit: skip download
        } catch (std::exception const &e) {
          // Cache miss: hash mismatch, delete and re-download
          tui::trace("phase fetch: cache mismatch for %s, deleting",
                     dest.string().c_str());
          std::filesystem::remove(dest);
          to_download.push_back(i);
        }
      } else {
        // No SHA256: always re-download (no cache trust)
        tui::trace("phase fetch: no SHA256 for %s, re-downloading (no cache)",
                   dest.filename().string().c_str());
        to_download.push_back(i);
      }
    } else {
      // File doesn't exist: download
      to_download.push_back(i);
    }
  }

  return to_download;
}

// Verify a downloaded file's SHA256 if provided.
void verify_downloaded_file(fetch_spec const &spec, fetch_result const &result) {
  if (spec.sha256.empty()) { return; }

  tui::trace("phase fetch: verifying SHA256 for %s",
             result.resolved_destination.string().c_str());
  sha256_verify(spec.sha256, sha256(result.resolved_destination));
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
      errors.push_back(specs[spec_idx].request.source + ": " + *err);
    } else {
      // File downloaded successfully
#ifdef ENVY_FUNCTIONAL_TESTER
      try {
        test::decrement_fail_counter();
      } catch (std::exception const &e) {
        errors.push_back(specs[spec_idx].request.source + ": " + e.what());
        continue;
      }
#endif

      try {
        auto const *result{ std::get_if<fetch_result>(&results[i]) };
        if (!result) { throw std::runtime_error("Unexpected result type"); }
        verify_downloaded_file(specs[spec_idx], *result);
      } catch (std::exception const &e) {
        errors.push_back(specs[spec_idx].request.source + ": " + e.what());
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

}  // namespace

void run_fetch_phase(std::string const &key, graph_state &state) {
  tui::trace("phase fetch START %s", key.c_str());
  trace_on_exit trace_end{ "phase fetch END " + key };

  auto [lua, lock] = [&] {
    typename decltype(state.recipes)::accessor acc;
    if (!state.recipes.find(acc, key)) {
      throw std::runtime_error("Recipe not found for " + key);
    }
    return std::pair{ acc->second.lua_state.get(), acc->second.lock.get() };
  }();

  if (!lock) {
    tui::trace("phase fetch: no lock (cache hit), skipping");
    return;
  }

  if (lock->is_fetch_complete()) {
    tui::trace("phase fetch: fetch already complete, skipping");
    return;
  }

  lua_getglobal(lua, "fetch");
  int const fetch_type{ lua_type(lua, -1) };

  if (fetch_type == LUA_TFUNCTION) {
    lua_pop(lua, 1);
    tui::trace("phase fetch: has fetch function, skipping declarative (Phase 4)");
    return;
  }

  if (fetch_type == LUA_TNIL) {
    lua_pop(lua, 1);
    tui::trace("phase fetch: no fetch field, skipping");
    return;
  }

  // Parse fetch field into specs
  auto fetch_specs{ parse_fetch_field(lua, lock->fetch_dir(), key) };
  lua_pop(lua, 1);

  if (fetch_specs.empty()) { return; }

  // Determine which files need downloading based on cache
  auto to_download{ determine_downloads_needed(fetch_specs) };

  // Execute downloads and verification
  execute_downloads(fetch_specs, to_download, key);

  lock->mark_fetch_complete();
  tui::trace("phase fetch: marked fetch complete");
}

}  // namespace envy
