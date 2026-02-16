#pragma once

#include "cmd.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace CLI { class App; }

namespace envy {

class cmd_sync : public cmd {
 public:
  struct cfg : cmd_cfg<cmd_sync> {
    std::vector<std::string> identities;  // Optional: if empty, sync all manifest packages
    std::optional<std::filesystem::path> manifest_path;
    bool install_all = false;  // If true, install packages; otherwise only deploy scripts
    bool strict = false;  // If true, error on non-envy-managed product script conflicts
    std::string platform_flag;  // "posix", "windows", "all", or empty (current OS)
  };

  static void register_cli(CLI::App &app, std::function<void(cfg)> on_selected);

  cmd_sync(cfg cfg, std::optional<std::filesystem::path> const &cli_cache_root);

  void execute() override;
  cfg const &get_cfg() const { return cfg_; }

 private:
  cfg cfg_;
  std::optional<std::filesystem::path> cli_cache_root_;
};

}  // namespace envy
