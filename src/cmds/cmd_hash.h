#pragma once

#include "cmd.h"

#include <filesystem>

namespace envy {

class cmd_hash : public cmd {
 public:
  struct cfg : cmd_cfg<cmd_hash> {
    std::filesystem::path file_path;
  };

  cmd_hash(cfg cfg, cache &c);

  void execute() override;
  cfg const &get_cfg() const { return cfg_; }

 private:
  cfg cfg_;
};

}  // namespace envy
