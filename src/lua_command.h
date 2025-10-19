#pragma once

#include <filesystem>
#include "command.h"

namespace envy {

class lua_command : public command {
 public:
  struct config : command_cfg<lua_command> {
    std::filesystem::path script_path;
  };

  explicit lua_command(config cfg);

  void schedule(tbb::flow::graph &g) override;

 private:
  config config_;
};

}  // namespace envy
