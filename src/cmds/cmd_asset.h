#pragma once

#include "cmd.h"

#include <filesystem>
#include <optional>
#include <string>

namespace envy {

class cmd_asset : public cmd {
 public:
  struct cfg : cmd_cfg<cmd_asset> {
    std::string identity;  // Required: "namespace.name@version"
    std::optional<std::filesystem::path> manifest_path;
  };

  cmd_asset(cfg cfg, cache &c);

  void execute() override;
  cfg const &get_cfg() const { return cfg_; }

 private:
  cfg cfg_;
  cache &cache_;
};

}  // namespace envy
