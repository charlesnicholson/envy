#pragma once

#include "cache.h"
#include "lua_util.h"

#include <tbb/flow_graph.h>

#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace envy {

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

  // Declared dependencies (identity strings) for validation
  std::vector<std::string> declared_dependencies;
};

}  // namespace envy
