#pragma once

#include "cmd.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace CLI { class App; }

namespace envy {

class cmd_engine_functional_test : public cmd {
 public:
  struct cfg : cmd_cfg<cmd_engine_functional_test> {
    std::string identity;
    std::filesystem::path spec_path;
    int fail_after_fetch_count = -1;  // -1 = disabled, >0 = fail after N files
  };

  static void register_cli(CLI::App &app, std::function<void(cfg)> on_selected);

  cmd_engine_functional_test(cfg cfg,
                             std::optional<std::filesystem::path> const &cli_cache_root);

  void execute() override;
  cfg const &get_cfg() const { return cfg_; }

 private:
  cfg cfg_;
  std::optional<std::filesystem::path> cli_cache_root_;
};

}  // namespace envy
