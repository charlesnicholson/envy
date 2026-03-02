#pragma once

#include "cmd.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace CLI { class App; }

namespace envy {

class cmd_hash : public cmd {
 public:
  struct cfg : cmd_cfg<cmd_hash> {
    std::vector<std::filesystem::path> paths;
    std::optional<std::string> prefix;
  };

  static void register_cli(CLI::App &app, std::function<void(cfg)> on_selected);

  cmd_hash(cfg cfg, std::optional<std::filesystem::path> const &cli_cache_root);

  void execute() override;
  cfg const &get_cfg() const { return cfg_; }

 private:
  cfg cfg_;
};

}  // namespace envy
