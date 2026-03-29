#pragma once

#include "cmd.h"

#include <filesystem>
#include <functional>
#include <iosfwd>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace CLI { class App; }

namespace envy {

struct depot_manifest_entry {
  std::string hash;  // lowercase 64-char hex
  std::string path;
};

std::vector<depot_manifest_entry> parse_depot_manifest(std::filesystem::path const &file);
std::unordered_set<std::string> parse_s3_ls_lines(std::istream &in);

class cmd_merge_depot : public cmd {
 public:
  enum class retain_format { PLAIN, S3_LS };

  struct retain_source {
    std::string path;
    retain_format fmt{ retain_format::PLAIN };
  };

  struct cfg : cmd_cfg<cmd_merge_depot> {
    std::vector<std::filesystem::path> depot_manifests;
    std::optional<std::string> existing_path;
    std::optional<retain_source> retain;
    std::optional<std::string> retain_prefix;
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
