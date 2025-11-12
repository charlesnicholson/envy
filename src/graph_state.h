#pragma once

#include "cache.h"
#include "lua_util.h"
#include "recipe.h"
#include "tui.h"

#include <tbb/concurrent_hash_map.h>
#include <tbb/concurrent_unordered_set.h>
#include <tbb/flow_graph.h>

#include <string>
#include <unordered_map>
#include <utility>

namespace envy {

struct manifest;

struct trace_on_exit {
  std::string message;
  explicit trace_on_exit(std::string msg) : message{ std::move(msg) } {}
  ~trace_on_exit() { tui::trace("%s", message.c_str()); }
};

struct graph_state {
  tbb::flow::graph &graph;
  cache &cache_;
  manifest const *manifest_;  // Manifest (for default_shell resolution, always non-null)

  using node_ptr = recipe::node_ptr;
  tbb::concurrent_hash_map<std::string, recipe> recipes;
  tbb::concurrent_unordered_set<std::string> triggered;
  tbb::concurrent_unordered_set<std::string> executed;
};

std::string make_canonical_key(std::string const &identity,
                               std::unordered_map<std::string, lua_value> const &options);

}  // namespace envy
