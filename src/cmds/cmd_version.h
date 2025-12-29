#pragma once

#include "cmd.h"

#include <filesystem>
#include <functional>
#include <optional>

namespace CLI { class App; }

namespace envy {

class cmd_version : public cmd {
 public:
  struct cfg : cmd_cfg<cmd_version> {
    bool show_licenses{ false };
  };

  static void register_cli(CLI::App &app, std::function<void(cfg)> on_selected);

  cmd_version(cfg cfg, std::optional<std::filesystem::path> const &cli_cache_root);

  void execute() override;
  cfg const &get_cfg() const { return cfg_; }

 private:
  cfg cfg_;
};

}  // namespace envy
