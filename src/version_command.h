#pragma once

#include "command.h"

#include "tbb/flow_graph.h"

#include <optional>

namespace envy {

class version_command : public command {
 public:
  struct config : command_cfg<version_command> {};

  explicit version_command(config cfg);
  void schedule(tbb::flow::graph &g) override;

 private:
  config config_;

  std::optional<tbb::flow::continue_node<tbb::flow::continue_msg>> node_;
};

}  // namespace envy
