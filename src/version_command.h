#pragma once

#include "command.h"

namespace envy {

class version_command : public command {
 public:
  struct config : command_cfg<version_command> {};

  explicit version_command(config cfg);
  void schedule(tbb::flow::graph &g) override;

 private:
  config config_;
};

}  // namespace envy
