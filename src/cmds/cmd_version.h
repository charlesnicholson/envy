#pragma once

#include "cmd.h"

namespace envy {

class cmd_version : public cmd {
 public:
  struct cfg : cmd_cfg<cmd_version> {};

  cmd_version(cfg cfg, cache &c);

  void execute() override;
  cfg const &get_cfg() const { return cfg_; }

 private:
  cfg cfg_;
};

}  // namespace envy
