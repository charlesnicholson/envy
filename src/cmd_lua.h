#pragma once

#include "cmd.h"

#include <filesystem>

namespace envy {

class cmd_lua : public cmd {
 public:
  struct config : cmd_cfg<cmd_lua> {
    std::filesystem::path script_path;
  };

  explicit cmd_lua(config cfg);

  void schedule(tbb::flow::graph &g) override;

  config const &get_config() const { return config_; }

 private:
  config config_;
};

}  // namespace envy
