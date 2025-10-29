#pragma once

#include "cmd.h"

#include <string>

namespace envy {

class cmd_playground : public cmd {
 public:
  struct cfg : cmd_cfg<cmd_playground> {
    std::string uri;
    std::string region;
  };

  explicit cmd_playground(cfg cfg);

  bool execute() override;
  cfg const &get_cfg() const { return cfg_; }

 private:
  cfg cfg_;
};

}  // namespace envy
