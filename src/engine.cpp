#include "engine.h"

#include "cache.h"
#include "fetch.h"
#include "lua_util.h"
#include "tui.h"

extern "C" {
#include "lua.h"
}

#include <tbb/concurrent_hash_map.h>
#include <tbb/concurrent_unordered_map.h>
#include <tbb/concurrent_unordered_set.h>
#include <tbb/flow_graph.h>

#include <algorithm>
#include <memory>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace envy {

namespace {

struct recipe {
  using node_ptr = std::shared_ptr<tbb::flow::continue_node<tbb::flow::continue_msg>>;

  node_ptr fetch_node;
  node_ptr execute_node;
  lua_state_ptr lua_state;  // null until fetch completes
  std::string result_hash;  // empty until execute completes
};

struct graph_state {
  tbb::flow::graph &graph;
  cache &cache_;

  using node_ptr = std::shared_ptr<tbb::flow::continue_node<tbb::flow::continue_msg>>;
  tbb::concurrent_hash_map<std::string, recipe> recipes;
  tbb::concurrent_unordered_set<std::string> triggered;
  tbb::concurrent_unordered_set<std::string> executed;
};

std::string make_canonical_key(
    std::string const &identity,
    std::unordered_map<std::string, std::string> const &options) {
  if (options.empty()) { return identity; }

  std::vector<std::pair<std::string, std::string>> sorted{ options.begin(),
                                                           options.end() };
  std::ranges::sort(sorted);

  std::ostringstream oss;
  oss << identity << '{';
  bool first{ true };
  for (auto const &[k, v] : sorted) {
    if (!first) oss << ',';
    oss << k << '=' << v;
    first = false;
  }
  oss << '}';
  return oss.str();
}

void validate_phases(lua_State *lua, std::string const &identity) {
  lua_getglobal(lua, "fetch");
  bool const has_fetch{ lua_isfunction(lua, -1) };
  lua_pop(lua, 1);

  if (has_fetch) { return; }  // fetch alone is valid

  // No fetch, so check + install are required
  lua_getglobal(lua, "check");
  bool const has_check{ lua_isfunction(lua, -1) };
  lua_pop(lua, 1);

  lua_getglobal(lua, "install");
  bool const has_install{ lua_isfunction(lua, -1) };
  lua_pop(lua, 1);

  if (!has_check || !has_install) {
    throw std::runtime_error("Recipe must define 'fetch' or both 'check' and 'install': " +
                             identity);
  }
}

void create_recipe_nodes(recipe_spec const &spec,
                         graph_state &state,
                         std::unordered_set<std::string> const &ancestors);

void execute_recipe_phases(std::string const &key, graph_state &state) {
  // Idempotency check - atomically check if already executed
  // This allows execute to be triggered multiple times safely (from fetch→execute edge
  // AND from dependency edges) without duplicate work or race conditions
  auto [iter, inserted]{ state.executed.insert(key) };
  if (!inserted) { return; }  // Already executed by another thread

  // Access lua state via const_accessor (read-only)
  lua_State *lua{ [&] {
    typename decltype(state.recipes)::const_accessor acc;
    if (!state.recipes.find(acc, key)) {
      throw std::runtime_error("Recipe not found for " + key);
    }
    return acc->second.lua_state.get();  // Get raw pointer while recipe stays in map
  }() };

  // Execute check() phase
  lua_getglobal(lua, "check");
  bool const has_check{ lua_isfunction(lua, -1) };
  bool check_result{ true };

  if (has_check) {
    tui::trace("phase check START %s", key.c_str());
    lua_newtable(lua);

    if (lua_pcall(lua, 1, 1, 0) != LUA_OK) {
      char const *err{ lua_tostring(lua, -1) };
      lua_pop(lua, 1);
      throw std::runtime_error("check() failed for " + key + ": " +
                               (err ? err : "unknown error"));
    }

    check_result = lua_toboolean(lua, -1);
    lua_pop(lua, 1);
    tui::trace("phase check END %s (result=%s)",
               key.c_str(),
               check_result ? "true" : "false");
  } else {
    lua_pop(lua, 1);
  }

  if (!check_result) {  // Execute install() phase if check returned false
    lua_getglobal(lua, "install");

    if (lua_isfunction(lua, -1)) {
      tui::trace("phase install START %s", key.c_str());
      lua_newtable(lua);

      if (lua_pcall(lua, 1, 0, 0) != LUA_OK) {
        char const *err{ lua_tostring(lua, -1) };
        lua_pop(lua, 1);
        throw std::runtime_error("install() failed for " + key + ": " +
                                 (err ? err : "unknown error"));
      }

      tui::trace("phase install END %s", key.c_str());
    } else {
      lua_pop(lua, 1);
    }
  }

  {  // Write result hash
    typename decltype(state.recipes)::accessor acc;
    if (state.recipes.find(acc, key)) { acc->second.result_hash = "STUB_HASH"; }
  }
}

void fetch_recipe_and_spawn_dependencies(
    recipe_spec const &spec,
    std::string const &key,
    graph_state &state,
    tbb::flow::continue_node<tbb::flow::continue_msg> *execute_ptr,
    std::unordered_set<std::string> const &ancestors) {
  auto lua_state{ lua_make() };
  lua_add_envy(lua_state);

  // Load recipe from source
  std::filesystem::path recipe_path;
  if (auto const *local_src{ std::get_if<recipe_spec::local_source>(&spec.source) }) {
    recipe_path = local_src->file_path;
    if (!lua_run_file(lua_state, recipe_path)) {
      throw std::runtime_error("Failed to load recipe: " + spec.identity);
    }
  } else if (auto const *remote_src{
                 std::get_if<recipe_spec::remote_source>(&spec.source) }) {
    // Ensure recipe in cache
    auto cache_result{ state.cache_.ensure_recipe(spec.identity) };

    if (cache_result.lock) {  // We won the race - fetch recipe into cache
      tui::trace("fetch recipe %s from %s",
                 spec.identity.c_str(),
                 remote_src->url.c_str());
      std::filesystem::path fetch_dest{ cache_result.lock->install_dir() / "recipe.lua" };

      // Note: remote_source.subdir is not yet implemented
      // Currently only single-file recipes (.lua) are supported for remote sources
      // Archive extraction with subdir navigation will be added when needed
      fetch({ .source = remote_src->url, .destination = fetch_dest });

      cache_result.lock->mark_install_complete();
      cache_result.lock.reset();  // Release lock, moving install_dir to asset_path
    }

    recipe_path = cache_result.asset_path / "recipe.lua";
    if (!lua_run_file(lua_state, recipe_path)) {
      throw std::runtime_error("Failed to load recipe: " + spec.identity);
    }
  } else {
    throw std::runtime_error("Only local and remote sources supported: " + spec.identity);
  }

  validate_phases(lua_state.get(), spec.identity);

  // Parse dependencies (before moving lua_state)
  std::vector<recipe_spec> dep_configs;
  if (auto const deps_array{ lua_global_to_array(lua_state.get(), "dependencies") }) {
    for (auto const &dep_val : *deps_array) {
      auto dep_cfg{ recipe_spec::parse(dep_val, recipe_path) };

      // Security: non-local.* recipes cannot depend on local.* recipes
      if (!spec.identity.starts_with("local.") && dep_cfg.identity.starts_with("local.")) {
        throw std::runtime_error("Security violation: non-local recipe '" + spec.identity +
                                 "' cannot depend on local recipe '" + dep_cfg.identity +
                                 "'");
      }

      dep_configs.push_back(dep_cfg);
    }
  }

  {  // Store lua state using accessor for thread-safe insertion
    typename decltype(state.recipes)::accessor acc;
    if (state.recipes.find(acc, key)) { acc->second.lua_state = std::move(lua_state); }
  }

  // Build ancestor chain for cycle detection - add current node to ancestors
  std::unordered_set<std::string> dep_ancestors{ ancestors };
  dep_ancestors.insert(key);

  // Process dependencies: create nodes, connect edges, and trigger
  for (auto const &dep_cfg : dep_configs) {
    auto const dep_key{ make_canonical_key(dep_cfg.identity, dep_cfg.options) };

    // Create dependency nodes - pass ancestors for cycle detection
    create_recipe_nodes(dep_cfg, state, dep_ancestors);

    {  // Connect edge: dep execute → self execute
      typename decltype(state.recipes)::const_accessor acc;
      if (state.recipes.find(acc, dep_key)) {
        tbb::flow::make_edge(*acc->second.execute_node, *execute_ptr);
      }
    }

    // Try to insert into triggered set; if successful, trigger the node
    auto [iter, inserted]{ state.triggered.insert(dep_key) };
    if (inserted) {
      typename decltype(state.recipes)::const_accessor acc;
      if (state.recipes.find(acc, dep_key)) {
        acc->second.fetch_node->try_put(tbb::flow::continue_msg{});
      }
    }
  }
}

void create_recipe_nodes(recipe_spec const &spec,
                         graph_state &state,
                         std::unordered_set<std::string> const &ancestors = {}) {
  auto const key{ make_canonical_key(spec.identity, spec.options) };

  // Cycle detection: check if this recipe is in our ancestor chain
  if (ancestors.contains(key)) {
    throw std::runtime_error("Cycle detected: " + key + " depends on itself");
  }

  {  // Check if recipe already exists (use const_accessor for read-only check)
    typename decltype(state.recipes)::const_accessor acc;
    if (state.recipes.find(acc, key)) { return; }
  }

  // Validate identity matches source type
  if (spec.identity.starts_with("local.") && !spec.is_local()) {
    throw std::runtime_error("Recipe 'local.*' must have local source: " + spec.identity);
  }

  // Create execute node upfront (will access recipe_data later when it runs)
  auto execute_node{ std::make_shared<tbb::flow::continue_node<tbb::flow::continue_msg>>(
      state.graph,
      [key, &state](tbb::flow::continue_msg const &) {
        execute_recipe_phases(key, state);
      }) };

  // Create fetch node - loads recipe and spawns dependencies
  // Capture ancestors by value so each node has its own chain
  auto fetch_node{ std::make_shared<tbb::flow::continue_node<tbb::flow::continue_msg>>(
      state.graph,
      [spec, key, &state, execute_ptr = execute_node.get(), ancestors](
          tbb::flow::continue_msg const &) {
        fetch_recipe_and_spawn_dependencies(spec, key, state, execute_ptr, ancestors);
      }) };

  {  // Store recipe with both nodes using accessor for thread-safe insertion
    typename decltype(state.recipes)::accessor acc;
    if (state.recipes.insert(acc, key)) {
      acc->second.fetch_node = fetch_node;
      acc->second.execute_node = execute_node;
    }
  }
}

}  // namespace

recipe_asset_hash_map_t engine_run(std::vector<recipe_spec> const &roots, cache &cache_) {
  tbb::flow::graph flow_graph;
  graph_state state{ .graph = flow_graph, .cache_ = cache_ };

  for (auto const &cfg : roots) {  // Create nodes and trigger fetch for all roots
    auto const key{ make_canonical_key(cfg.identity, cfg.options) };
    create_recipe_nodes(cfg, state);
    state.triggered.insert(key);

    typename decltype(state.recipes)::const_accessor acc;
    if (state.recipes.find(acc, key)) {
      acc->second.fetch_node->try_put(tbb::flow::continue_msg{});
    }
  }

  flow_graph.wait_for_all();

  // Trigger all execute nodes (edges will handle dependency ordering)
  // This happens after all fetches complete, ensuring all edges are connected
  for (auto const &[key, rec] : state.recipes) {
    rec.execute_node->try_put(tbb::flow::continue_msg{});
  }

  flow_graph.wait_for_all();

  recipe_asset_hash_map_t result;
  for (auto const &[key, rec] : state.recipes) { result[key] = rec.result_hash; }
  return result;
}

}  // namespace envy
