#include "phase_fetch.h"

#include "fetch.h"
#include "sha256.h"
#include "tui.h"

#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace envy {

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

  struct fetch_spec {
    fetch_request request;
    std::string sha256;
  };
  std::vector<fetch_spec> fetch_specs;
  std::unordered_set<std::string> basenames;

  if (fetch_type == LUA_TSTRING) {
    char const *url{ lua_tostring(lua, -1) };
    std::filesystem::path dest{ lock->fetch_dir() /
                                std::filesystem::path(url).filename() };
    std::string basename{ dest.filename().string() };

    if (basenames.contains(basename)) {
      lua_pop(lua, 1);
      throw std::runtime_error("Fetch filename collision: " + basename + " in " + key);
    }
    basenames.insert(basename);

    fetch_specs.push_back(
        { .request = { .source = url, .destination = dest }, .sha256 = "" });
  } else if (fetch_type == LUA_TTABLE) {
    lua_rawgeti(lua, -1, 1);
    bool const is_array{ !lua_isnil(lua, -1) };
    lua_pop(lua, 1);

    if (is_array) {
      size_t const len{ lua_rawlen(lua, -1) };
      for (size_t i = 1; i <= len; ++i) {
        lua_rawgeti(lua, -1, i);
        if (!lua_istable(lua, -1)) {
          lua_pop(lua, 2);
          throw std::runtime_error("Fetch array element must be table in " + key);
        }

        lua_getfield(lua, -1, "url");
        if (!lua_isstring(lua, -1)) {
          lua_pop(lua, 3);
          throw std::runtime_error("Fetch element missing 'url' field in " + key);
        }
        std::string url{ lua_tostring(lua, -1) };
        lua_pop(lua, 1);

        lua_getfield(lua, -1, "sha256");
        std::string sha256;
        if (lua_isstring(lua, -1)) { sha256 = lua_tostring(lua, -1); }
        lua_pop(lua, 1);

        std::filesystem::path dest{ lock->fetch_dir() /
                                    std::filesystem::path(url).filename() };
        std::string basename{ dest.filename().string() };

        if (basenames.contains(basename)) {
          lua_pop(lua, 2);
          throw std::runtime_error("Fetch filename collision: " + basename + " in " + key);
        }
        basenames.insert(basename);

        fetch_specs.push_back(
            { .request = { .source = url, .destination = dest }, .sha256 = sha256 });

        lua_pop(lua, 1);
      }
    } else {
      lua_getfield(lua, -1, "url");
      if (!lua_isstring(lua, -1)) {
        lua_pop(lua, 2);
        throw std::runtime_error("Fetch table missing 'url' field in " + key);
      }
      std::string url{ lua_tostring(lua, -1) };
      lua_pop(lua, 1);

      lua_getfield(lua, -1, "sha256");
      std::string sha256;
      if (lua_isstring(lua, -1)) { sha256 = lua_tostring(lua, -1); }
      lua_pop(lua, 1);

      std::filesystem::path dest{ lock->fetch_dir() /
                                  std::filesystem::path(url).filename() };
      fetch_specs.push_back(
          { .request = { .source = url, .destination = dest }, .sha256 = sha256 });
    }
  } else {
    lua_pop(lua, 1);
    throw std::runtime_error("Fetch field must be string, table, or function in " + key);
  }

  lua_pop(lua, 1);

  if (!fetch_specs.empty()) {
    tui::trace("phase fetch: downloading %zu file(s)", fetch_specs.size());

    std::vector<fetch_request> requests;
    requests.reserve(fetch_specs.size());
    for (auto const &spec : fetch_specs) { requests.push_back(spec.request); }

    auto const results{ fetch(requests) };

    std::vector<std::string> errors;
    for (size_t i = 0; i < results.size(); ++i) {
      if (auto const *err{ std::get_if<std::string>(&results[i]) }) {
        errors.push_back(fetch_specs[i].request.source + ": " + *err);
      } else if (!fetch_specs[i].sha256.empty()) {
        try {
          auto const *result{ std::get_if<fetch_result>(&results[i]) };
          if (!result) { throw std::runtime_error("Unexpected result type"); }

          tui::trace("phase fetch: verifying SHA256 for %s",
                     result->resolved_destination.string().c_str());
          sha256_verify(fetch_specs[i].sha256, sha256(result->resolved_destination));
        } catch (std::exception const &e) {
          errors.push_back(fetch_specs[i].request.source + ": " + e.what());
        }
      }
    }

    if (!errors.empty()) {
      std::ostringstream oss;
      oss << "Fetch failed for " << key << ":\n";
      for (auto const &err : errors) { oss << "  " << err << "\n"; }
      throw std::runtime_error(oss.str());
    }

    lock->mark_fetch_complete();
    tui::trace("phase fetch: marked fetch complete");
  }
}

}  // namespace envy
