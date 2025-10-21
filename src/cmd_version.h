#pragma once

#include "cmd.h"

#include "tbb/flow_graph.h"

#include <optional>

namespace envy {

class cmd_version : public cmd {
 public:
  struct cfg : cmd_cfg<cmd_version> {};

  explicit cmd_version(cfg cfg);

  void schedule(tbb::flow::graph &g) override;
  cfg const &get_cfg() const { return cfg_; }

 private:
  cfg cfg_;
  std::optional<tbb::flow::continue_node<tbb::flow::continue_msg>> node_;
};

}  // namespace envy
