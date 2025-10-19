#pragma once

#include "cmd.h"

#include <filesystem>

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
};

}  // namespace envy
