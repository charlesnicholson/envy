#pragma once

#include "cmd.h"

namespace envy {

class cmd_version : public cmd {
 public:
  struct cfg : cmd_cfg<cmd_version> {};

  explicit cmd_version(cfg cfg);

  bool execute() override;
  cfg const &get_cfg() const { return cfg_; }

 private:
  cfg cfg_;
};

}  // namespace envy
