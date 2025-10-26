#pragma once

#include "cmd.h"

#include <filesystem>
#include <string>

namespace envy {

struct cache_test_result {
  bool locked;
  bool fast_path;
  std::filesystem::path entry_path;
  std::filesystem::path install_path;
  std::filesystem::path work_path;
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
    std::filesystem::path cache_root;
    std::string test_id;
    std::filesystem::path barrier_dir;  // empty = use default
    std::string barrier_signal;         // empty = no barrier
    std::string barrier_wait;           // empty = no barrier
    std::string barrier_signal_after;   // signal after lock acquired
    std::string barrier_wait_after;     // wait after lock acquired
    int crash_after_ms = -1;            // -1 = no crash
    bool fail_before_complete = false;
  };

  explicit cmd_cache_ensure_asset(cfg const &config);
  void schedule(tbb::flow::graph &g) override;

 private:
  cfg cfg_;
};

class cmd_cache_ensure_recipe : public cmd {
 public:
  struct cfg : cmd_cfg<cmd_cache_ensure_recipe> {
    std::string identity;
    std::filesystem::path cache_root;
    std::string test_id;
    std::filesystem::path barrier_dir;  // empty = use default
    std::string barrier_signal;         // empty = no barrier
    std::string barrier_wait;           // empty = no barrier
    std::string barrier_signal_after;   // signal after lock acquired
    std::string barrier_wait_after;     // wait after lock acquired
    int crash_after_ms = -1;            // -1 = no crash
    bool fail_before_complete = false;
  };

  explicit cmd_cache_ensure_recipe(cfg const &config);
  void schedule(tbb::flow::graph &g) override;

 private:
  cfg cfg_;
};

}  // namespace envy
