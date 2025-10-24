#include "cmd_cache_functional_test.h"

#include "cache.h"
#include "tui.h"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <thread>

namespace envy {

class test_barrier {
 public:
  explicit test_barrier(std::filesystem::path const &barrier_dir)
      : barrier_dir_{ barrier_dir } {
    std::filesystem::create_directories(barrier_dir_);
  }

  void signal(std::string const &name) {
    if (name.empty()) { return; }
    std::filesystem::path marker{ barrier_dir_ / name };
    std::ofstream{ marker }.close();
  }

  void wait(std::string const &name) {
    if (name.empty()) { return; }
    std::filesystem::path marker{ barrier_dir_ / name };
    while (!std::filesystem::exists(marker)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }

 private:
  std::filesystem::path barrier_dir_;
};

// Convert cache test result to key=value format
std::string cache_test_result::to_keyvalue() const {
  std::ostringstream oss;
  oss << "locked=" << (locked ? "true" : "false") << '\n';
  oss << "fast_path=" << (fast_path ? "true" : "false") << '\n';
  oss << "path=" << path.string() << '\n';
  oss << "entry_path=" << entry_path.string() << '\n';
  oss << "lock_file=" << lock_file.string() << '\n';
  return oss.str();
}

// cmd_cache_ensure_asset implementation
cmd_cache_ensure_asset::cmd_cache_ensure_asset(cfg const &config) : cfg_{ config } {}

void cmd_cache_ensure_asset::schedule(tbb::flow::graph &g) {
  auto *node{ new tbb::flow::continue_node<tbb::flow::continue_msg>(
      g,
      [this](tbb::flow::continue_msg) {
        try {
          // Set up barrier coordination
          std::filesystem::path barrier_dir{
            cfg_.barrier_dir.empty()
                ? std::filesystem::temp_directory_path() / ("envy-barrier-" + cfg_.test_id)
                : cfg_.barrier_dir
          };
          test_barrier barrier{ barrier_dir };

          // Signal barrier first if requested (before starting work)
          barrier.signal(cfg_.barrier_signal);

          // Wait for barrier if requested (before attempting lock)
          barrier.wait(cfg_.barrier_wait);

          // Create cache and ensure asset
          cache c{ cfg_.cache_root };
          auto result{
            c.ensure_asset(cfg_.identity, cfg_.platform, cfg_.arch, cfg_.hash_prefix)
          };

          // Construct lock file path for reporting
          std::string entry_name{ cfg_.identity + "." + cfg_.platform + "-" + cfg_.arch +
                                  "-sha256-" + cfg_.hash_prefix };
          std::filesystem::path lock_file{ c.root() / "locks" /
                                           ("deployed." + entry_name + ".lock") };

          // Determine result state
          bool locked{ result.lock.has_value() };
          bool fast_path{ !locked };

          // Signal/wait after lock acquired if requested
          barrier.signal(cfg_.barrier_signal_after);
          barrier.wait(cfg_.barrier_wait_after);

          // Crash injection for testing
          if (cfg_.crash_after_ms >= 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(cfg_.crash_after_ms));
            std::abort();
          }

          // Fail before complete for testing
          if (cfg_.fail_before_complete) {
            succeeded_ = false;
            cache_test_result output{ locked,
                                      fast_path,
                                      result.path,
                                      c.root() / "deployed" / entry_name,
                                      lock_file };
            tui::print_stdout("%s", output.to_keyvalue().c_str());
            return;
          }

          // Mark complete if we got the lock
          if (result.lock.has_value()) { result.lock->mark_complete(); }

          // Output result
          cache_test_result output{ locked,
                                    fast_path,
                                    result.path,
                                    c.root() / "deployed" / entry_name,
                                    lock_file };
          tui::print_stdout("%s", output.to_keyvalue().c_str());

          succeeded_ = true;
        } catch (std::exception const &ex) {
          tui::error("Cache ensure-asset failed: %s", ex.what());
          succeeded_ = false;
        }
      }) };
  node->try_put(tbb::flow::continue_msg{});
}

// cmd_cache_ensure_recipe implementation
cmd_cache_ensure_recipe::cmd_cache_ensure_recipe(cfg const &config) : cfg_{ config } {}

void cmd_cache_ensure_recipe::schedule(tbb::flow::graph &g) {
  auto *node{ new tbb::flow::continue_node<tbb::flow::continue_msg>(
      g,
      [this](tbb::flow::continue_msg) {
        try {
          // Set up barrier coordination
          std::filesystem::path barrier_dir{
            cfg_.barrier_dir.empty()
                ? std::filesystem::temp_directory_path() / ("envy-barrier-" + cfg_.test_id)
                : cfg_.barrier_dir
          };
          test_barrier barrier{ barrier_dir };

          // Signal barrier first if requested (before starting work)
          barrier.signal(cfg_.barrier_signal);

          // Wait for barrier if requested (before attempting lock)
          barrier.wait(cfg_.barrier_wait);

          // Create cache and ensure recipe
          cache c{ cfg_.cache_root };
          auto result{ c.ensure_recipe(cfg_.identity) };

          // Construct lock file path for reporting
          std::filesystem::path lock_file{ c.root() / "locks" /
                                           ("recipe." + cfg_.identity + ".lock") };

          // Determine result state
          bool locked{ result.lock.has_value() };
          bool fast_path{ !locked };

          // Signal/wait after lock acquired if requested
          barrier.signal(cfg_.barrier_signal_after);
          barrier.wait(cfg_.barrier_wait_after);

          // Crash injection for testing
          if (cfg_.crash_after_ms >= 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(cfg_.crash_after_ms));
            std::abort();
          }

          // Fail before complete for testing
          if (cfg_.fail_before_complete) {
            succeeded_ = false;
            cache_test_result output{ locked,
                                      fast_path,
                                      result.path,
                                      c.root() / "recipes" / (cfg_.identity + ".lua"),
                                      lock_file };
            tui::print_stdout("%s", output.to_keyvalue().c_str());
            return;
          }

          // Mark complete if we got the lock
          if (result.lock.has_value()) { result.lock->mark_complete(); }

          // Output result
          cache_test_result output{ locked,
                                    fast_path,
                                    result.path,
                                    c.root() / "recipes" / (cfg_.identity + ".lua"),
                                    lock_file };
          tui::print_stdout("%s", output.to_keyvalue().c_str());

          succeeded_ = true;
        } catch (std::exception const &ex) {
          tui::error("Cache ensure-recipe failed: %s", ex.what());
          succeeded_ = false;
        }
      }) };
  node->try_put(tbb::flow::continue_msg{});
}

}  // namespace envy
