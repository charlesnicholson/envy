#pragma once

#include "cmd.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace CLI { class App; }

namespace envy {

class cmd_package : public cmd {
 public:
  struct cfg : cmd_cfg<cmd_package> {
    std::string identity;  // Required: "namespace.name@version"
    std::optional<std::filesystem::path> manifest_path;
  };

  static void register_cli(CLI::App &app, std::function<void(cfg)> on_selected);

  cmd_package(cfg cfg, std::optional<std::filesystem::path> const &cli_cache_root);

  void execute() override;
  cfg const &get_cfg() const { return cfg_; }

 private:
  cfg cfg_;
  std::optional<std::filesystem::path> cli_cache_root_;
};

}  // namespace envy
