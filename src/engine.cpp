#include "engine.h"

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

struct recipe_data {
  lua_state_ptr lua_state;

  // Default constructor for TBB concurrent_hash_map (lua_state needs explicit deleter)
  recipe_data() : lua_state{ nullptr, lua_close } {}
  recipe_data(recipe_data &&) = default;
  recipe_data &operator=(recipe_data &&) = default;
};

struct graph_state {
  tbb::flow::graph &graph;

  // Thread-safe maps for tracking nodes (use shared_ptr for concurrent insertion)
  using node_ptr = std::shared_ptr<tbb::flow::continue_node<tbb::flow::continue_msg>>;
  tbb::concurrent_hash_map<std::string, node_ptr> fetch_nodes;
  tbb::concurrent_hash_map<std::string, node_ptr> execute_nodes;
  tbb::concurrent_hash_map<std::string, recipe_data> recipe_data_map;

  // Thread-safe result map
  tbb::concurrent_unordered_map<std::string, std::string> result;

  // Thread-safe triggering control
  tbb::concurrent_unordered_set<std::string> triggered;
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

void validate_phases(lua_State *L, std::string const &identity) {
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

// Forward declaration for use in helper functions
void create_recipe_nodes(recipe cfg,
                         graph_state &state,
                         std::unordered_set<std::string> const &ancestors);

void execute_recipe_phases(std::string const &key, graph_state &state) {
  // Access recipe data via const_accessor (read-only)
  typename decltype(state.recipe_data_map)::const_accessor data_acc;
  if (!state.recipe_data_map.find(data_acc, key)) {
    throw std::runtime_error("Recipe data not found for " + key);
  }
  lua_State *L{ data_acc->second.lua_state.get() };

  // Execute check() phase
  lua_getglobal(L, "check");
  bool const has_check{ lua_isfunction(L, -1) };
  bool check_result{ true };

  if (has_check) {
    tui::trace("phase check START %s", key.c_str());
    lua_newtable(L);

    if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
      char const *err{ lua_tostring(L, -1) };
      lua_pop(L, 1);
      throw std::runtime_error("check() failed for " + key + ": " +
                               (err ? err : "unknown error"));
    }

    check_result = lua_toboolean(L, -1);
    lua_pop(L, 1);
    tui::trace("phase check END %s (result=%s)",
               key.c_str(),
               check_result ? "true" : "false");
  } else {
    lua_pop(L, 1);
  }

  // Execute install() phase if check returned false
  if (!check_result) {
    lua_getglobal(L, "install");
    bool const has_install{ lua_isfunction(L, -1) };

    if (has_install) {
      tui::trace("phase install START %s", key.c_str());
      lua_newtable(L);

      if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
        char const *err{ lua_tostring(L, -1) };
        lua_pop(L, 1);
        throw std::runtime_error("install() failed for " + key + ": " +
                                 (err ? err : "unknown error"));
      }

      tui::trace("phase install END %s", key.c_str());
    } else {
      lua_pop(L, 1);
    }
  }

  // Add result (concurrent_unordered_map::insert is thread-safe)
  state.result.insert({ key, "STUB_HASH" });
}

void fetch_recipe_and_spawn_dependencies(
    recipe const &cfg,
    std::string const &key,
    graph_state &state,
    tbb::flow::continue_node<tbb::flow::continue_msg> *execute_ptr,
    std::unordered_set<std::string> const &ancestors) {
  // Load recipe Lua file
  auto lua_state{ lua_make() };
  lua_add_envy(lua_state);

  // Determine recipe path
  auto const *local_src{ std::get_if<recipe::local_source>(&cfg.source) };
  if (!local_src) {
    throw std::runtime_error("Only local recipes supported in minimal implementation");
  }

  if (!lua_run_file(lua_state, local_src->file_path)) {
    throw std::runtime_error("Failed to load recipe: " + cfg.identity);
  }

  validate_phases(lua_state.get(), cfg.identity);

  // Parse dependencies (before moving lua_state)
  std::vector<recipe> dep_configs;
  if (auto const deps_array{ lua_global_to_array(lua_state.get(), "dependencies") }) {
    for (auto const &dep_val : *deps_array) {
      auto dep_cfg{ recipe::parse(dep_val, local_src->file_path) };
      dep_configs.push_back(dep_cfg);
    }
  }

  // Store recipe data using accessor for thread-safe insertion
  {
    typename decltype(state.recipe_data_map)::accessor data_acc;
    state.recipe_data_map.insert(data_acc, key);
    data_acc->second.lua_state = std::move(lua_state);
  }

  // Build ancestor chain for cycle detection - add current node to ancestors
  std::unordered_set<std::string> dep_ancestors{ ancestors };
  dep_ancestors.insert(key);

  // Process dependencies: create nodes, connect edges, and trigger
  for (auto const &dep_cfg : dep_configs) {
    auto const dep_key{ make_canonical_key(dep_cfg.identity, dep_cfg.options) };

    // Create dependency nodes - pass ancestors for cycle detection
    create_recipe_nodes(dep_cfg, state, dep_ancestors);

    // Connect edge: dep execute → self execute
    {
      typename decltype(state.execute_nodes)::const_accessor dep_acc;
      if (state.execute_nodes.find(dep_acc, dep_key)) {
        tbb::flow::make_edge(*dep_acc->second, *execute_ptr);
      }
    }

    // Try to insert into triggered set; if successful, trigger the node
    auto [iter, inserted]{ state.triggered.insert(dep_key) };
    if (inserted) {
      typename decltype(state.fetch_nodes)::const_accessor fetch_acc;
      if (state.fetch_nodes.find(fetch_acc, dep_key)) {
        fetch_acc->second->try_put(tbb::flow::continue_msg{});
      }
    }
  }

  // If no dependencies, trigger execute immediately
  if (dep_configs.empty()) { execute_ptr->try_put(tbb::flow::continue_msg{}); }
}

void create_recipe_nodes(recipe cfg,
                         graph_state &state,
                         std::unordered_set<std::string> const &ancestors = {}) {
  auto const key{ make_canonical_key(cfg.identity, cfg.options) };

  // Cycle detection: check if this recipe is in our ancestor chain
  if (ancestors.contains(key)) {
    throw std::runtime_error("Cycle detected: " + key + " depends on itself");
  }

  // Check if nodes already exist (use const_accessor for read-only check)
  {
    typename decltype(state.fetch_nodes)::const_accessor acc;
    if (state.fetch_nodes.find(acc, key)) { return; }
  }

  // Validate identity matches source type
  if (cfg.identity.starts_with("local.") && !cfg.is_local()) {
    throw std::runtime_error("Recipe 'local.*' must have local source: " + cfg.identity);
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
      [cfg, key, &state, execute_ptr = execute_node.get(), ancestors](
          tbb::flow::continue_msg const &) {
        fetch_recipe_and_spawn_dependencies(cfg, key, state, execute_ptr, ancestors);
      }) };

  // Store both nodes using accessors for thread-safe insertion
  {
    typename decltype(state.execute_nodes)::accessor exec_acc;
    if (state.execute_nodes.insert(exec_acc, key)) { exec_acc->second = execute_node; }
  }
  {
    typename decltype(state.fetch_nodes)::accessor fetch_acc;
    if (state.fetch_nodes.insert(fetch_acc, key)) { fetch_acc->second = fetch_node; }
  }
}

}  // namespace

recipe_asset_hash_map_t engine_run(std::vector<recipe> const &roots, cache &) {
  tbb::flow::graph flow_graph;
  graph_state state{ .graph = flow_graph };

  // Create nodes and trigger fetch for all root recipes
  for (auto const &cfg : roots) {
    auto const key{ make_canonical_key(cfg.identity, cfg.options) };

    create_recipe_nodes(cfg, state);

    // Mark as triggered and trigger the fetch node
    state.triggered.insert(key);
    typename decltype(state.fetch_nodes)::const_accessor fetch_acc;
    if (state.fetch_nodes.find(fetch_acc, key)) {
      fetch_acc->second->try_put(tbb::flow::continue_msg{});
    }
  }

  // Wait for all work to complete
  flow_graph.wait_for_all();

  // Convert concurrent_unordered_map to regular unordered_map
  recipe_asset_hash_map_t result;
  for (auto const &[k, v] : state.result) { result[k] = v; }
  return result;
}

}  // namespace envy
