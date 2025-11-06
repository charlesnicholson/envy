#include "engine.h"

#include "blake3_util.h"
#include "cache.h"
#include "fetch.h"
#include "lua_util.h"
#include "sha256.h"
#include "tui.h"
#include "util.h"

extern "C" {
#include "lua.h"
}

#include <tbb/concurrent_hash_map.h>
#include <tbb/concurrent_unordered_map.h>
#include <tbb/concurrent_unordered_set.h>
#include <tbb/flow_graph.h>

#include <algorithm>
#include <atomic>
#include <memory>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace envy {

namespace {

struct trace_on_exit {
  std::string message;
  explicit trace_on_exit(std::string msg) : message{ std::move(msg) } {}
  ~trace_on_exit() { tui::trace("%s", message.c_str()); }
};

struct recipe {
  using node_ptr = std::shared_ptr<tbb::flow::continue_node<tbb::flow::continue_msg>>;

  // Phase nodes (all created upfront)
  node_ptr recipe_fetch_node;
  node_ptr check_node;
  node_ptr fetch_node;
  node_ptr stage_node;
  node_ptr build_node;
  node_ptr install_node;
  node_ptr deploy_node;
  node_ptr completion_node;

  // Phase state
  lua_state_ptr lua_state;               // set by recipe_fetch
  cache::scoped_entry_lock::ptr_t lock;  // set by check if cache miss
  std::filesystem::path asset_path;      // set by check if cache hit
  std::string result_hash;               // set by completion

  std::atomic_bool completed{ false };  // race guard: finished before dependants start
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
  int const fetch_type{ lua_type(lua, -1) };
  bool const has_fetch{ fetch_type == LUA_TFUNCTION || fetch_type == LUA_TSTRING ||
                        fetch_type == LUA_TTABLE };
  lua_pop(lua, 1);

  if (has_fetch) { return; }  // fetch alone is valid (function, string, or table)

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

// Forward declarations for phase functions
void create_recipe_nodes(std::string const &key,
                         recipe_spec const &spec,
                         graph_state &state,
                         std::unordered_set<std::string> const &ancestors);

void run_check_phase(std::string const &key, graph_state &state);
void run_fetch_phase(std::string const &key, graph_state &state);
void run_stage_phase(std::string const &key, graph_state &state);
void run_build_phase(std::string const &key, graph_state &state);
void run_install_phase(std::string const &key, graph_state &state);
void run_deploy_phase(std::string const &key, graph_state &state);
void run_completion_phase(std::string const &key, graph_state &state);

void run_recipe_fetch_phase(recipe_spec const &spec,
                            std::string const &key,
                            graph_state &state,
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
      auto const results{ fetch(
          { { .source = remote_src->url, .destination = fetch_dest } }) };
      if (results.empty() || std::holds_alternative<std::string>(results[0])) {
        throw std::runtime_error(
            "Failed to fetch recipe: " +
            (results.empty() ? "no results" : std::get<std::string>(results[0])));
      }

      if (!remote_src->sha256.empty()) {
        tui::trace("verifying SHA256 for recipe %s", spec.identity.c_str());
        sha256_verify(remote_src->sha256, sha256(fetch_dest));
      }

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

  // Validate recipe identity declaration
  std::string const declared_identity = [&] {
    try {
      return lua_global_to_string(lua_state.get(), "identity");
    } catch (std::runtime_error const &e) {
      throw std::runtime_error(std::string(e.what()) + " (in recipe: " + spec.identity +
                               ")");
    }
  }();

  if (declared_identity != spec.identity) {
    throw std::runtime_error("Identity mismatch: expected '" + spec.identity +
                             "' but recipe declares '" + declared_identity + "'");
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
    create_recipe_nodes(dep_key, dep_cfg, state, dep_ancestors);

    {  // Connect edge: dep.completion → parent.check
       // TODO: Implement needed_by logic in step 4
      typename decltype(state.recipes)::const_accessor dep_acc, parent_acc;
      if (state.recipes.find(dep_acc, dep_key) && state.recipes.find(parent_acc, key)) {
        // ALWAYS add the edge first
        tbb::flow::make_edge(*dep_acc->second.completion_node,
                             *parent_acc->second.check_node);

        // Check if dependency already completed before this recipe started!
        if (dep_acc->second.completed) {
          parent_acc->second.check_node->try_put(tbb::flow::continue_msg{});
        }
      }
    }

    // Try to insert into triggered set; if successful, trigger the node
    auto [iter, inserted]{ state.triggered.insert(dep_key) };
    if (inserted) {
      typename decltype(state.recipes)::const_accessor acc;
      if (state.recipes.find(acc, dep_key)) {
        acc->second.recipe_fetch_node->try_put(tbb::flow::continue_msg{});
      }
    }
  }
}

// Stub implementations (will be filled in later steps)
void run_check_phase(std::string const &key, graph_state &state) {
  tui::trace("phase check START %s", key.c_str());
  trace_on_exit trace_end{ "phase check END " + key };

  // Get lua_state and check if user defined check() function
  lua_State *lua{ [&] {
    typename decltype(state.recipes)::const_accessor acc;
    if (!state.recipes.find(acc, key)) {
      throw std::runtime_error("Recipe not found for " + key);
    }
    return acc->second.lua_state.get();
  }() };

  // Check if user defined a check() function
  lua_getglobal(lua, "check");
  bool const has_check{ lua_isfunction(lua, -1) };

  if (has_check) {
    // Call check() - return whether package is installed
    lua_newtable(lua);

    if (lua_pcall(lua, 1, 1, 0) != LUA_OK) {
      char const *err{ lua_tostring(lua, -1) };
      lua_pop(lua, 1);
      throw std::runtime_error("check() failed for " + key + ": " +
                               (err ? err : "unknown error"));
    }

    bool const installed{ static_cast<bool>(lua_toboolean(lua, -1)) };
    lua_pop(lua, 1);

    if (installed) {
      typename decltype(state.recipes)::accessor acc;
      if (state.recipes.find(acc, key)) {
        // For now, use a placeholder asset_path since user-defined check
        // doesn't tell us where the asset is
        acc->second.asset_path = "/placeholder/asset/path";
        acc->second.completion_node->try_put(tbb::flow::continue_msg{});
      }
      tui::trace("phase check: user check returned true, triggered completion");
      return;
    }
  } else {
    lua_pop(lua, 1);
  }

  // Not installed or no check() - call cache.ensure_asset to get lock or existing asset
  // For now, use a simple hash derived from the key
  // TODO: Compute proper content hash based on sources and dependencies
  auto const digest{ blake3_hash(key.data(), key.size()) };

  std::string const hash_prefix{ util_bytes_to_hex(digest.data(), 8) };

  std::string const platform{ lua_global_to_string(lua, "ENVY_PLATFORM") };
  std::string const arch{ lua_global_to_string(lua, "ENVY_ARCH") };

  // Extract identity from key (remove options suffix if present)
  std::string identity{ key };
  if (auto const brace_pos{ key.find('{') }; brace_pos != std::string::npos) {
    identity = key.substr(0, brace_pos);
  }

  auto cache_result{ state.cache_.ensure_asset(identity, platform, arch, hash_prefix) };

  // Store result and trigger appropriate next phase
  typename decltype(state.recipes)::accessor acc;
  if (state.recipes.find(acc, key)) {
    if (cache_result.lock) {
      // Cache miss - we won the race, need to build
      acc->second.lock = std::move(cache_result.lock);
      tui::trace("phase check: cache miss for %s, triggering fetch", key.c_str());
      // Wire deploy -> completion edge now (will be used later)
      tbb::flow::make_edge(*acc->second.deploy_node, *acc->second.completion_node);
      // Trigger fetch phase directly
      acc->second.fetch_node->try_put(tbb::flow::continue_msg{});
    } else {
      // Cache hit - asset already exists
      acc->second.asset_path = cache_result.asset_path;
      tui::trace("phase check: cache hit for %s, triggering completion", key.c_str());
      // Trigger completion phase directly
      acc->second.completion_node->try_put(tbb::flow::continue_msg{});
    }
  }
}

void run_fetch_phase(std::string const &key, graph_state &state) {
  tui::trace("phase fetch START %s", key.c_str());
  trace_on_exit trace_end{ "phase fetch END " + key };

  // Get lua_state and lock
  auto [lua, lock] = [&] {
    typename decltype(state.recipes)::accessor acc;
    if (!state.recipes.find(acc, key)) {
      throw std::runtime_error("Recipe not found for " + key);
    }
    return std::pair{ acc->second.lua_state.get(), acc->second.lock.get() };
  }();

  if (!lock) {  // cache hit path - skip fetch
    tui::trace("phase fetch: no lock (cache hit), skipping");
    return;
  }

  if (lock->is_fetch_complete()) {  // fetch is complete (previous build etc failed)
    tui::trace("phase fetch: fetch already complete, skipping");
    return;
  }

  // Get fetch field from Lua
  lua_getglobal(lua, "fetch");
  int const fetch_type{ lua_type(lua, -1) };

  if (fetch_type == LUA_TFUNCTION) {  // Imperative fetch - defer to Phase 4
    lua_pop(lua, 1);
    tui::trace("phase fetch: has fetch function, skipping declarative (Phase 4)");
    return;
  }

  if (fetch_type == LUA_TNIL) {  // No fetch field - valid if recipe has check+install
    lua_pop(lua, 1);
    tui::trace("phase fetch: no fetch field, skipping");
    return;
  }

  // Parse declarative fetch (string or table)
  struct fetch_spec {
    fetch_request request;
    std::string sha256;  // Optional verification hash
  };
  std::vector<fetch_spec> fetch_specs;
  std::unordered_set<std::string> basenames;  // For collision detection

  if (fetch_type == LUA_TSTRING) {
    // String: fetch = "url"
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
    // Table: could be single fetch or array of fetches
    // Check if it's an array (has numeric keys starting at 1)
    lua_rawgeti(lua, -1, 1);
    bool const is_array{ !lua_isnil(lua, -1) };
    lua_pop(lua, 1);

    if (is_array) {
      // Array of fetches: fetch = {{url="..."}, {url="..."}}
      size_t const len{ lua_rawlen(lua, -1) };
      for (size_t i = 1; i <= len; ++i) {
        lua_rawgeti(lua, -1, i);
        if (!lua_istable(lua, -1)) {
          lua_pop(lua, 2);  // pop element and fetch table
          throw std::runtime_error("Fetch array element must be table in " + key);
        }

        // Get url field
        lua_getfield(lua, -1, "url");
        if (!lua_isstring(lua, -1)) {
          lua_pop(lua, 3);  // pop url, element, fetch table
          throw std::runtime_error("Fetch element missing 'url' field in " + key);
        }
        std::string url{ lua_tostring(lua, -1) };
        lua_pop(lua, 1);  // pop url

        // Get optional sha256 field
        lua_getfield(lua, -1, "sha256");
        std::string sha256;
        if (lua_isstring(lua, -1)) { sha256 = lua_tostring(lua, -1); }
        lua_pop(lua, 1);  // pop sha256

        std::filesystem::path dest{ lock->fetch_dir() /
                                    std::filesystem::path(url).filename() };
        std::string basename{ dest.filename().string() };

        if (basenames.contains(basename)) {
          lua_pop(lua, 2);  // pop element and fetch table
          throw std::runtime_error("Fetch filename collision: " + basename + " in " + key);
        }
        basenames.insert(basename);

        fetch_specs.push_back(
            { .request = { .source = url, .destination = dest }, .sha256 = sha256 });

        lua_pop(lua, 1);  // pop element
      }
    } else {
      // Single fetch: fetch = {url="...", sha256="..."}
      lua_getfield(lua, -1, "url");
      if (!lua_isstring(lua, -1)) {
        lua_pop(lua, 2);  // pop url and fetch table
        throw std::runtime_error("Fetch table missing 'url' field in " + key);
      }
      std::string url{ lua_tostring(lua, -1) };
      lua_pop(lua, 1);  // pop url

      // Get optional sha256 field
      lua_getfield(lua, -1, "sha256");
      std::string sha256;
      if (lua_isstring(lua, -1)) { sha256 = lua_tostring(lua, -1); }
      lua_pop(lua, 1);  // pop sha256

      std::filesystem::path dest{ lock->fetch_dir() /
                                  std::filesystem::path(url).filename() };
      fetch_specs.push_back(
          { .request = { .source = url, .destination = dest }, .sha256 = sha256 });
    }
  } else {
    lua_pop(lua, 1);
    throw std::runtime_error("Fetch field must be string, table, or function in " + key);
  }

  lua_pop(lua, 1);  // pop fetch value

  // Execute fetches
  if (!fetch_specs.empty()) {
    tui::trace("phase fetch: downloading %zu file(s)", fetch_specs.size());

    // Build requests vector
    std::vector<fetch_request> requests;
    requests.reserve(fetch_specs.size());
    for (auto const &spec : fetch_specs) { requests.push_back(spec.request); }

    auto const results{ fetch(requests) };

    // Check for errors and verify SHA256
    std::vector<std::string> errors;
    for (size_t i = 0; i < results.size(); ++i) {
      if (auto const *err{ std::get_if<std::string>(&results[i]) }) {
        errors.push_back(fetch_specs[i].request.source + ": " + *err);
      } else if (!fetch_specs[i].sha256.empty()) {
        // Verify SHA256 if provided
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

    // Mark fetch complete
    lock->mark_fetch_complete();
    tui::trace("phase fetch: marked fetch complete");
  }
}

void run_stage_phase(std::string const &key, graph_state &state) {
  tui::trace("phase stage START %s", key.c_str());
  trace_on_exit trace_end{ "phase stage END " + key };
  // TODO: Implement stage logic
}

void run_build_phase(std::string const &key, graph_state &state) {
  tui::trace("phase build START %s", key.c_str());
  trace_on_exit trace_end{ "phase build END " + key };
  // TODO: Implement build logic
}

void run_install_phase(std::string const &key, graph_state &state) {
  tui::trace("phase install START %s", key.c_str());
  trace_on_exit trace_end{ "phase install END " + key };

  // Get lua_state
  lua_State *lua{ [&] {
    typename decltype(state.recipes)::const_accessor acc;
    if (!state.recipes.find(acc, key)) {
      throw std::runtime_error("Recipe not found for " + key);
    }
    return acc->second.lua_state.get();
  }() };

  // Check if user defined install() function
  lua_getglobal(lua, "install");
  bool const has_install{ lua_isfunction(lua, -1) };

  if (has_install) {
    lua_newtable(lua);
    if (lua_pcall(lua, 1, 0, 0) != LUA_OK) {
      char const *err{ lua_tostring(lua, -1) };
      lua_pop(lua, 1);
      throw std::runtime_error("install() failed for " + key + ": " +
                               (err ? err : "unknown error"));
    }
  } else {
    lua_pop(lua, 1);  // Pop nil from failed getglobal
  }

  // Finalize install: mark complete and move to asset path
  typename decltype(state.recipes)::accessor acc;
  if (state.recipes.find(acc, key)) {
    if (acc->second.lock) {
      // Ensure install_dir exists
      auto const install_dir{ acc->second.lock->install_dir() };
      std::filesystem::create_directories(install_dir);

      // Get the entry_path to compute final asset_path
      // The lock destructor will rename install_dir to entry_path/asset
      std::filesystem::path const entry_path{ install_dir.parent_path() };
      std::filesystem::path const final_asset_path{ entry_path / "asset" };

      acc->second.lock->mark_install_complete();
      acc->second.asset_path = final_asset_path;
      acc->second.lock.reset();  // Release lock, which moves install_dir to asset_dir
    }
  }
}

void run_deploy_phase(std::string const &key, graph_state &state) {
  tui::trace("phase deploy START %s", key.c_str());
  trace_on_exit trace_end{ "phase deploy END " + key };
  // TODO: Implement deploy logic
}

void run_completion_phase(std::string const &key, graph_state &state) {
  tui::trace("phase completion START %s", key.c_str());
  trace_on_exit trace_end{ "phase completion END " + key };

  typename decltype(state.recipes)::accessor acc;
  if (!state.recipes.find(acc, key)) {
    throw std::runtime_error("Recipe not found in completion phase: " + key);
  }

  // Compute result hash from asset_path
  // TODO: Compute proper content hash of installed assets
  // For now, use a simple hash of the path
  if (!acc->second.asset_path.empty()) {
    auto const path_str{ acc->second.asset_path.string() };
    acc->second.result_hash =
        path_str.length() >= 16 ? path_str.substr(path_str.length() - 16) : path_str;

    acc->second.completed = true;

    tui::trace("phase completion: computed result_hash=%s for %s",
               acc->second.result_hash.c_str(),
               key.c_str());
  } else {
    // asset_path empty means check phase set neither lock nor asset_path
    tui::warn("phase completion: asset_path EMPTY for %s", key.c_str());
    throw std::runtime_error("Completion phase: asset_path not set for recipe: " + key);
  }
}

void create_recipe_nodes(std::string const &key,
                         recipe_spec const &spec,
                         graph_state &state,
                         std::unordered_set<std::string> const &ancestors = {}) {
  if (ancestors.contains(key)) {
    throw std::runtime_error("Cycle detected: " + key + " depends on itself");
  }

  {  // Check if recipe already exists (use const_accessor for read-only check)
    typename decltype(state.recipes)::const_accessor acc;
    if (state.recipes.find(acc, key)) { return; }
  }

  if (spec.identity.starts_with("local.") && !spec.is_local()) {
    throw std::runtime_error("Recipe 'local.*' must have local source: " + spec.identity);
  }

  // Create ALL phase nodes upfront
  auto recipe_fetch_node{
    std::make_shared<tbb::flow::continue_node<tbb::flow::continue_msg>>(
        state.graph,
        [spec, key, &state, ancestors](tbb::flow::continue_msg const &) {
          run_recipe_fetch_phase(spec, key, state, ancestors);
        })
  };

  auto check_node{ std::make_shared<tbb::flow::continue_node<tbb::flow::continue_msg>>(
      state.graph,
      [key, &state](tbb::flow::continue_msg const &) { run_check_phase(key, state); }) };

  auto fetch_node{ std::make_shared<tbb::flow::continue_node<tbb::flow::continue_msg>>(
      state.graph,
      [key, &state](tbb::flow::continue_msg const &) { run_fetch_phase(key, state); }) };

  auto stage_node{ std::make_shared<tbb::flow::continue_node<tbb::flow::continue_msg>>(
      state.graph,
      [key, &state](tbb::flow::continue_msg const &) { run_stage_phase(key, state); }) };

  auto build_node{ std::make_shared<tbb::flow::continue_node<tbb::flow::continue_msg>>(
      state.graph,
      [key, &state](tbb::flow::continue_msg const &) { run_build_phase(key, state); }) };

  auto install_node{ std::make_shared<tbb::flow::continue_node<tbb::flow::continue_msg>>(
      state.graph,
      [key, &state](tbb::flow::continue_msg const &) { run_install_phase(key, state); }) };

  auto deploy_node{ std::make_shared<tbb::flow::continue_node<tbb::flow::continue_msg>>(
      state.graph,
      [key, &state](tbb::flow::continue_msg const &) { run_deploy_phase(key, state); }) };

  auto completion_node{
    std::make_shared<tbb::flow::continue_node<tbb::flow::continue_msg>>(
        state.graph,
        [key, &state](tbb::flow::continue_msg const &) {
          run_completion_phase(key, state);
        })
  };

  // Create intra-recipe edges (linear chain)
  // Note: check->fetch, deploy->completion, and check->completion edges are created
  // dynamically by run_check_phase based on cache hit/miss
  tbb::flow::make_edge(*recipe_fetch_node, *check_node);
  tbb::flow::make_edge(*fetch_node, *stage_node);
  tbb::flow::make_edge(*stage_node, *build_node);
  tbb::flow::make_edge(*build_node, *install_node);
  tbb::flow::make_edge(*install_node, *deploy_node);

  {  // Store recipe with all nodes using accessor for thread-safe insertion
    typename decltype(state.recipes)::accessor acc;
    if (state.recipes.insert(acc, key)) {
      acc->second.recipe_fetch_node = recipe_fetch_node;
      acc->second.check_node = check_node;
      acc->second.fetch_node = fetch_node;
      acc->second.stage_node = stage_node;
      acc->second.build_node = build_node;
      acc->second.install_node = install_node;
      acc->second.deploy_node = deploy_node;
      acc->second.completion_node = completion_node;
    }
  }
}

}  // namespace

recipe_asset_hash_map_t engine_run(std::vector<recipe_spec> const &roots, cache &cache_) {
  tbb::flow::graph flow_graph;
  graph_state state{ .graph = flow_graph, .cache_ = cache_ };

  // Create nodes and trigger recipe_fetch for all roots
  for (auto const &cfg : roots) {
    auto const key{ make_canonical_key(cfg.identity, cfg.options) };
    create_recipe_nodes(key, cfg, state);
    state.triggered.insert(key);

    typename decltype(state.recipes)::const_accessor acc;
    if (state.recipes.find(acc, key)) {
      acc->second.recipe_fetch_node->try_put(tbb::flow::continue_msg{});
    }
  }

  // Wait for graph to complete (all phases run naturally via edges)
  flow_graph.wait_for_all();

  // Collect results from completion phase
  recipe_asset_hash_map_t result;
  for (auto const &[key, rec] : state.recipes) {
    typename decltype(state.recipes)::const_accessor acc;
    state.recipes.find(acc, key);
    if (acc->second.result_hash.empty()) {
      tui::warn("Recipe %s has EMPTY result_hash after wait_for_all()", key.c_str());
      tui::warn("  asset_path: %s", acc->second.asset_path.string().c_str());
      tui::warn("  has lock: %s", acc->second.lock ? "yes" : "no");
    }
    result[key] = acc->second.result_hash;
  }
  return result;
}

}  // namespace envy
