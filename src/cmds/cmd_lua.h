#pragma once

#include "cmd.h"

#include <filesystem>

namespace envy {

class cmd_lua : public cmd {
 public:
  struct cfg : cmd_cfg<cmd_lua> {
    std::filesystem::path script_path;
  };

  cmd_lua(cfg cfg, cache &c);

  void execute() override;
  cfg const &get_cfg() const { return cfg_; }

 private:
  cfg cfg_;
};

}  // namespace envy
