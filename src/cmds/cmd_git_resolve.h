#pragma once

#include "cmd.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace CLI { class App; }

namespace envy {

class cmd_git_resolve : public cmd {
 public:
  struct cfg : cmd_cfg<cmd_git_resolve> {
    std::string repo;
    std::string ref;
  };

  static void register_cli(CLI::App &app, std::function<void(cfg)> on_selected);

  cmd_git_resolve(cfg cfg, std::optional<std::filesystem::path> const &cli_cache_root);

  void execute() override;

 private:
  cfg cfg_;
};

}  // namespace envy
