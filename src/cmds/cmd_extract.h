#pragma once

#include "cmd.h"

#include <filesystem>

namespace envy {

class cmd_extract : public cmd {
 public:
  struct cfg : cmd_cfg<cmd_extract> {
    std::filesystem::path archive_path;
    std::filesystem::path destination;
  };

  cmd_extract(cfg cfg, cache &c);

  void execute() override;
  cfg const &get_cfg() const { return cfg_; }

 private:
  cfg cfg_;
};

}  // namespace envy
