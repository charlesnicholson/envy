#pragma once

#include "cache.h"
#include "lua_util.h"
#include "recipe_spec.h"
#include "tui.h"

#include <tbb/concurrent_hash_map.h>
#include <tbb/concurrent_unordered_set.h>
#include <tbb/flow_graph.h>

#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

extern "C" {
#include "lua.h"
}

namespace envy {

struct trace_on_exit {
  std::string message;
  explicit trace_on_exit(std::string msg) : message{ std::move(msg) } {}
  ~trace_on_exit() { tui::trace("%s", message.c_str()); }
};

struct recipe {
  using node_ptr = std::shared_ptr<tbb::flow::continue_node<tbb::flow::continue_msg>>;

  node_ptr recipe_fetch_node;
  node_ptr check_node;
  node_ptr fetch_node;
  node_ptr stage_node;
  node_ptr build_node;
  node_ptr install_node;
  node_ptr deploy_node;
  node_ptr completion_node;

  lua_state_ptr lua_state;
  cache::scoped_entry_lock::ptr_t lock;
  std::filesystem::path asset_path;
  std::string result_hash;

  std::string identity;
  std::unordered_map<std::string, lua_value> options;

  std::atomic_bool completed{ false };
};

struct graph_state {
  tbb::flow::graph &graph;
  cache &cache_;

  using node_ptr = recipe::node_ptr;
  tbb::concurrent_hash_map<std::string, recipe> recipes;
  tbb::concurrent_unordered_set<std::string> triggered;
  tbb::concurrent_unordered_set<std::string> executed;
};

std::string make_canonical_key(std::string const &identity,
                               std::unordered_map<std::string, lua_value> const &options);

}  // namespace envy
