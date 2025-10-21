#pragma once

#include "cmd.h"

#include "tbb/flow_graph.h"

#include <filesystem>
#include <optional>

namespace envy {

class cmd_extract : public cmd {
 public:
  struct cfg : cmd_cfg<cmd_extract> {
    std::filesystem::path archive_path;
    std::filesystem::path destination;
  };

  explicit cmd_extract(cfg cfg);

  void schedule(tbb::flow::graph &g) override;
  cfg const &get_cfg() const { return cfg_; }

 private:
  cfg cfg_;
  std::optional<tbb::flow::continue_node<tbb::flow::continue_msg>> node_;
};

}  // namespace envy
