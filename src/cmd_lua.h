#pragma once

#include "cmd.h"

#include "tbb/flow_graph.h"

#include <filesystem>
#include <optional>

namespace envy {

class cmd_lua : public cmd {
 public:
  struct cfg : cmd_cfg<cmd_lua> {
    std::filesystem::path script_path;
  };

  explicit cmd_lua(cfg cfg);

  void schedule(tbb::flow::graph &g) override;
  cfg const &get_config() const { return cfg_; }

 private:
  cfg cfg_;
  std::optional<tbb::flow::continue_node<tbb::flow::continue_msg>> node_;
};

}  // namespace envy
