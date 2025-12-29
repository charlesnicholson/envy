#pragma once

#include "cmd.h"

#include <filesystem>
#include <optional>
#include <string>

namespace envy {

struct cache_test_result {
  bool locked{ false };
  bool fast_path{ false };
  std::filesystem::path entry_path;
  std::filesystem::path asset_path;
  std::filesystem::path install_path;
  std::filesystem::path fetch_path;
  std::filesystem::path stage_path;
  std::filesystem::path lock_file;

  std::string to_keyvalue() const;
};

class cmd_cache_ensure_asset : public cmd {
 public:
  struct cfg : cmd_cfg<cmd_cache_ensure_asset> {
    std::string identity;
    std::string platform;
    std::string arch;
    std::string hash_prefix;
    std::string test_id;
    std::filesystem::path barrier_dir;  // empty = use default
    std::string barrier_signal;         // empty = no barrier
    std::string barrier_wait;           // empty = no barrier
    std::string barrier_signal_after;   // signal after lock acquired
    std::string barrier_wait_after;     // wait after lock acquired
    int crash_after_ms = -1;            // -1 = no crash
    bool fail_before_complete = false;
  };

  cmd_cache_ensure_asset(cfg const &config,
                         std::optional<std::filesystem::path> const &cli_cache_root);
  void execute() override;

 private:
  cfg cfg_;
  std::optional<std::filesystem::path> cli_cache_root_;
};

class cmd_cache_ensure_recipe : public cmd {
 public:
  struct cfg : cmd_cfg<cmd_cache_ensure_recipe> {
    std::string identity;
    std::string test_id;
    std::filesystem::path barrier_dir;  // empty = use default
    std::string barrier_signal;         // empty = no barrier
    std::string barrier_wait;           // empty = no barrier
    std::string barrier_signal_after;   // signal after lock acquired
    std::string barrier_wait_after;     // wait after lock acquired
    int crash_after_ms = -1;            // -1 = no crash
    bool fail_before_complete = false;
  };

  cmd_cache_ensure_recipe(cfg const &config,
                          std::optional<std::filesystem::path> const &cli_cache_root);
  void execute() override;

 private:
  cfg cfg_;
  std::optional<std::filesystem::path> cli_cache_root_;
};

}  // namespace envy
