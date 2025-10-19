#pragma once

#include "cmd.h"

#include "tbb/flow_graph.h"

#include <optional>

namespace envy {

class cmd_version : public cmd {
 public:
  struct config : cmd_cfg<cmd_version> {};

  explicit cmd_version(config cfg);
  void schedule(tbb::flow::graph &g) override;

  config const &get_config() const { return config_; }

 private:
  config config_;

  std::optional<tbb::flow::continue_node<tbb::flow::continue_msg>> node_;
};

}  // namespace envy
