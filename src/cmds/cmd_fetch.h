#pragma once

#include "cmd.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace CLI { class App; }

namespace envy {

class cmd_fetch : public cmd {
 public:
  struct cfg : cmd_cfg<cmd_fetch> {
    std::string source;
    std::filesystem::path destination;
    std::optional<std::filesystem::path> manifest_root;
    std::optional<std::string> ref;
  };

  static void register_cli(CLI::App &app, std::function<void(cfg)> on_selected);

  cmd_fetch(cfg cfg, std::optional<std::filesystem::path> const &cli_cache_root);

  void execute() override;
  cfg const &get_cfg() const { return cfg_; }

 private:
  cfg cfg_;
};

}  // namespace envy
