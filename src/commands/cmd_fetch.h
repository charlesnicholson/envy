#pragma once

#include "cmd.h"

#include <filesystem>
#include <optional>
#include <string>

namespace envy {

class cmd_fetch : public cmd {
 public:
  struct cfg : cmd_cfg<cmd_fetch> {
    std::string source;
    std::filesystem::path destination;
    std::optional<std::filesystem::path> manifest_root;
  };

  explicit cmd_fetch(cfg cfg);

  bool execute() override;
  cfg const &get_cfg() const { return cfg_; }

 private:
  cfg cfg_;
};

}  // namespace envy
