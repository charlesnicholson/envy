#pragma once

#include "cmd.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace CLI { class App; }

namespace envy {

class cmd_import : public cmd {
 public:
  struct cfg : cmd_cfg<cmd_import> {
    std::filesystem::path archive_path;
  };

  static void register_cli(CLI::App &app, std::function<void(cfg)> on_selected);

  cmd_import(cfg cfg, std::optional<std::filesystem::path> const &cli_cache_root);

  void execute() override;

 private:
  cfg cfg_;
  std::optional<std::filesystem::path> cli_cache_root_;
};

#ifdef ENVY_UNIT_TEST
struct parsed_export_filename {
  std::string identity;
  std::string platform;
  std::string arch;
  std::string hash_prefix;
};

parsed_export_filename parse_export_filename(std::string_view stem);
#endif

}  // namespace envy
