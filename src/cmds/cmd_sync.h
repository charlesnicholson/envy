#pragma once

#include "cmd.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace envy {

class cmd_sync : public cmd {
 public:
  struct cfg : cmd_cfg<cmd_sync> {
    std::vector<std::string> identities;  // Optional: if empty, sync all manifest packages
    std::optional<std::filesystem::path> manifest_path;
  };

  cmd_sync(cfg cfg, cache &c);

  void execute() override;
  cfg const &get_cfg() const { return cfg_; }

 private:
  cfg cfg_;
  cache &cache_;
};

}  // namespace envy
