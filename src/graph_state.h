#pragma once

#include "cache.h"
#include "recipe.h"
#include "tui.h"

#include <tbb/flow_graph.h>

#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

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
  manifest const *manifest_;

  std::vector<std::unique_ptr<recipe>> recipes;
  std::mutex recipes_mutex;
};

}  // namespace envy
