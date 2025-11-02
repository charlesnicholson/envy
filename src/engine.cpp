#include "engine.h"

#include "lua_util.h"

#include <algorithm>
#include <unordered_set>
#include <vector>

namespace envy {

namespace {

void validate_phases(lua_State *L, std::string const &identity) {
  // Check for fetch function
  lua_getglobal(L, "fetch");
  bool const has_fetch{ lua_isfunction(L, -1) };
  lua_pop(L, 1);

  if (has_fetch) { return; }  // fetch alone is valid

  // No fetch, so check + install are required
  lua_getglobal(L, "check");
  bool const has_check{ lua_isfunction(L, -1) };
  lua_pop(L, 1);

  lua_getglobal(L, "install");
  bool const has_install{ lua_isfunction(L, -1) };
  lua_pop(L, 1);

  if (!has_check || !has_install) {
    throw std::runtime_error("Recipe must define 'fetch' or both 'check' and 'install': " +
                             identity);
  }
}

void resolve_recipe(recipe::cfg const &cfg,
                    recipe_asset_hash_map_t &result,
                    std::unordered_set<std::string> &visited,
                    std::vector<std::string> &path) {
  // Cycle detection
  if (std::ranges::find(path, cfg.identity) != path.end()) {
    std::string cycle_msg{ "Dependency cycle detected: " };
    for (auto const &id : path) { cycle_msg += id + " -> "; }
    cycle_msg += cfg.identity;
    throw std::runtime_error(cycle_msg);
  }

  // Skip if already resolved
  if (visited.contains(cfg.identity)) { return; }
  visited.insert(cfg.identity);

  path.push_back(cfg.identity);

  // Load recipe Lua file
  auto lua_state{ lua_make() };
  lua_add_envy(lua_state);

  // Determine recipe path
  auto const *local_src{ std::get_if<recipe::cfg::local_source>(&cfg.source) };
  if (!local_src) {
    throw std::runtime_error("Only local recipes supported in minimal implementation");
  }

  if (!lua_run_file(lua_state, local_src->file_path)) {
    throw std::runtime_error("Failed to load recipe: " + cfg.identity);
  }

  // Validate phases
  validate_phases(lua_state.get(), cfg.identity);

  // Parse dependencies
  if (auto const deps_array{ lua_global_to_array(lua_state.get(), "dependencies") }) {
    for (auto const &dep_val : *deps_array) {
      auto dep_cfg{ recipe::cfg::parse(dep_val, local_src->file_path) };
      resolve_recipe(dep_cfg, result, visited, path);
    }
  }

  path.pop_back();

  // Add this recipe to result
  result[cfg.identity] = "STUB_HASH";
}

}  // namespace

recipe_asset_hash_map_t engine_run(std::vector<recipe::cfg> const &roots, cache &c) {
  recipe_asset_hash_map_t result;
  std::unordered_set<std::string> visited;
  std::vector<std::string> path;

  for (auto const &cfg : roots) { resolve_recipe(cfg, result, visited, path); }

  return result;
}

}  // namespace envy
