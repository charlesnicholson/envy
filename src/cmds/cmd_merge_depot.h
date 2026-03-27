#pragma once

#include "cmd.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace CLI { class App; }

namespace envy {

struct depot_manifest_entry {
  std::string hash;  // lowercase 64-char hex
  std::string path;
};

std::vector<depot_manifest_entry> parse_depot_manifest(
    std::filesystem::path const &file);

class cmd_merge_depot : public cmd {
 public:
  struct cfg : cmd_cfg<cmd_merge_depot> {
    std::vector<std::filesystem::path> depot_manifests;
    std::optional<std::filesystem::path> existing_path;
    bool strict{ false };
  };

  static void register_cli(CLI::App &app, std::function<void(cfg)> on_selected);

  cmd_merge_depot(cfg cfg, std::optional<std::filesystem::path> const &cli_cache_root);

  void execute() override;
  cfg const &get_cfg() const { return cfg_; }

 private:
  cfg cfg_;
};

}  // namespace envy
