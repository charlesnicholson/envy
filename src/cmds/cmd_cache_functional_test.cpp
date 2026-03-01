#include "cmd_cache_functional_test.h"

#include "platform.h"
#include "self_deploy.h"
#include "tui.h"

#include "CLI11.hpp"

#include <chrono>
#include <cstdlib>
#include <memory>
#include <sstream>
#include <thread>

namespace envy {

void cmd_cache_ensure_package::register_cli(CLI::App &parent,
                                            std::function<void(cfg)> on_selected) {
  auto *sub{ parent.add_subcommand("ensure-package", "Test package cache entry") };
  auto cfg_ptr{ std::make_shared<cfg>() };
  sub->add_option("identity", cfg_ptr->identity, "Package identity")->required();
  sub->add_option("platform", cfg_ptr->platform, "Platform (darwin/linux/windows)")
      ->required();
  sub->add_option("arch", cfg_ptr->arch, "Architecture (arm64/x86_64)")->required();
  sub->add_option("hash_prefix", cfg_ptr->hash_prefix, "Hash prefix")->required();
  sub->add_option("--test-id", cfg_ptr->test_id, "Test ID for barrier isolation");
  sub->add_option("--barrier-dir", cfg_ptr->barrier_dir, "Barrier directory");
  sub->add_option("--barrier-signal",
                  cfg_ptr->barrier_signal,
                  "Barrier to signal before lock");
  sub->add_option("--barrier-wait",
                  cfg_ptr->barrier_wait,
                  "Barrier to wait for before lock");
  sub->add_option("--barrier-signal-after",
                  cfg_ptr->barrier_signal_after,
                  "Barrier to signal after lock");
  sub->add_option("--barrier-wait-after",
                  cfg_ptr->barrier_wait_after,
                  "Barrier to wait for after lock");
  sub->add_option("--crash-after", cfg_ptr->crash_after_ms, "Crash after N milliseconds");
  sub->add_flag("--fail-before-complete",
                cfg_ptr->fail_before_complete,
                "Exit without marking complete");
  sub->callback(
      [cfg_ptr, on_selected = std::move(on_selected)] { on_selected(*cfg_ptr); });
}

void cmd_cache_ensure_spec::register_cli(CLI::App &parent,
                                         std::function<void(cfg)> on_selected) {
  auto *sub{ parent.add_subcommand("ensure-spec", "Test spec cache entry") };
  auto cfg_ptr{ std::make_shared<cfg>() };
  sub->add_option("identity", cfg_ptr->identity, "Spec identity")->required();
  sub->add_option("--test-id", cfg_ptr->test_id, "Test ID for barrier isolation");
  sub->add_option("--barrier-dir", cfg_ptr->barrier_dir, "Barrier directory");
  sub->add_option("--barrier-signal",
                  cfg_ptr->barrier_signal,
                  "Barrier to signal before lock");
  sub->add_option("--barrier-wait",
                  cfg_ptr->barrier_wait,
                  "Barrier to wait for before lock");
  sub->add_option("--barrier-signal-after",
                  cfg_ptr->barrier_signal_after,
                  "Barrier to signal after lock");
  sub->add_option("--barrier-wait-after",
                  cfg_ptr->barrier_wait_after,
                  "Barrier to wait for after lock");
  sub->add_option("--crash-after", cfg_ptr->crash_after_ms, "Crash after N milliseconds");
  sub->add_flag("--fail-before-complete",
                cfg_ptr->fail_before_complete,
                "Exit without marking complete");
  sub->callback(
      [cfg_ptr, on_selected = std::move(on_selected)] { on_selected(*cfg_ptr); });
}

class test_barrier {
 public:
  explicit test_barrier(std::filesystem::path const &barrier_dir)
      : barrier_dir_{ barrier_dir } {
    std::filesystem::create_directories(barrier_dir_);
  }

  void signal(std::string const &name) {
    if (name.empty()) { return; }
    std::filesystem::path marker{ barrier_dir_ / name };
    platform::touch_file(marker);
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
  oss << "entry_path=" << entry_path.string() << '\n';
  oss << "pkg_path=" << pkg_path.string() << '\n';
  oss << "install_path=" << install_path.string() << '\n';
  oss << "fetch_path=" << fetch_path.string() << '\n';
  oss << "stage_path=" << stage_path.string() << '\n';
  oss << "lock_file=" << lock_file.string() << '\n';
  return oss.str();
}

// cmd_cache_ensure_package implementation
cmd_cache_ensure_package::cmd_cache_ensure_package(
    cfg const &config,
    std::optional<std::filesystem::path> const &cli_cache_root)
    : cfg_{ config }, cli_cache_root_{ cli_cache_root } {}

void cmd_cache_ensure_package::execute() {
  auto c{ self_deploy::ensure(cli_cache_root_, std::nullopt) };

  // Emit initial state so tests always have locked key even if we crash before output
  // later.
  tui::print_stdout("locked=false\nfast_path=false\n");
  // Set up barrier coordination
  std::filesystem::path barrier_dir{ cfg_.barrier_dir.empty()
                                         ? std::filesystem::temp_directory_path() /
                                               ("envy-barrier-" + cfg_.test_id)
                                         : cfg_.barrier_dir };
  test_barrier barrier{ barrier_dir };

  // Signal barrier first if requested (before starting work)
  barrier.signal(cfg_.barrier_signal);

  // Wait for barrier if requested (before attempting lock)
  barrier.wait(cfg_.barrier_wait);

  // Ensure pkg
  auto result{ c->ensure_pkg(cfg_.identity, cfg_.platform, cfg_.arch, cfg_.hash_prefix) };

  // Construct lock file path for reporting
  auto const k{
      cache::key(cfg_.identity, cfg_.platform, cfg_.arch, cfg_.hash_prefix) };
  std::filesystem::path lock_file{ c->root() / "locks" /
                                   ("packages." + k + ".lock") };

  // Determine result state
  bool locked{ result.lock != nullptr };
  bool fast_path{ !locked };

  // Signal/wait after lock acquired if requested
  barrier.signal(cfg_.barrier_signal_after);
  barrier.wait(cfg_.barrier_wait_after);

  // Crash injection for testing
  if (cfg_.crash_after_ms >= 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(cfg_.crash_after_ms));
    platform::terminate_process();
  }

  // Fail before complete for testing
  if (cfg_.fail_before_complete) {
    cache_test_result output{};
    output.locked = locked;
    output.fast_path = fast_path;
    output.entry_path = result.entry_path;
    output.pkg_path = result.pkg_path;
    if (result.lock) {
      output.install_path = result.lock->install_dir();
      output.fetch_path = result.lock->fetch_dir();
      output.stage_path = result.lock->stage_dir();
    }
    output.lock_file = lock_file;
    std::string const kv = output.to_keyvalue();
    tui::print_stdout("%s", kv.c_str());
    throw std::runtime_error("cache-test: fail_before_complete requested");
  }

  // Mark complete if we got the lock
  if (result.lock) { result.lock->mark_install_complete(); }

  // Output result
  cache_test_result output{};
  output.locked = locked;
  output.fast_path = fast_path;
  output.entry_path = result.entry_path;
  output.pkg_path = result.pkg_path;
  if (result.lock) {
    output.install_path = result.lock->install_dir();
    output.fetch_path = result.lock->fetch_dir();
    output.stage_path = result.lock->stage_dir();
  }
  output.lock_file = lock_file;
  std::string const kv = output.to_keyvalue();
  tui::print_stdout("%s", kv.c_str());
}

// cmd_cache_ensure_spec implementation
cmd_cache_ensure_spec::cmd_cache_ensure_spec(
    cfg const &config,
    std::optional<std::filesystem::path> const &cli_cache_root)
    : cfg_{ config }, cli_cache_root_{ cli_cache_root } {}

void cmd_cache_ensure_spec::execute() {
  auto c{ self_deploy::ensure(cli_cache_root_, std::nullopt) };

  tui::print_stdout("locked=false\nfast_path=false\n");
  // Set up barrier coordination
  std::filesystem::path barrier_dir{ cfg_.barrier_dir.empty()
                                         ? std::filesystem::temp_directory_path() /
                                               ("envy-barrier-" + cfg_.test_id)
                                         : cfg_.barrier_dir };
  test_barrier barrier{ barrier_dir };

  // Signal barrier first if requested (before starting work)
  barrier.signal(cfg_.barrier_signal);

  // Wait for barrier if requested (before attempting lock)
  barrier.wait(cfg_.barrier_wait);

  // Ensure spec
  auto result{ c->ensure_spec(cfg_.identity) };

  // Construct lock file path for reporting
  std::filesystem::path lock_file{ c->root() / "locks" /
                                   ("spec." + cfg_.identity + ".lock") };

  // Determine result state
  bool locked{ result.lock != nullptr };
  bool fast_path{ !locked };

  // Signal/wait after lock acquired if requested
  barrier.signal(cfg_.barrier_signal_after);
  barrier.wait(cfg_.barrier_wait_after);

  // Crash injection for testing
  if (cfg_.crash_after_ms >= 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(cfg_.crash_after_ms));
    platform::terminate_process();
  }

  // Fail before complete for testing
  if (cfg_.fail_before_complete) {
    cache_test_result output{};
    output.locked = locked;
    output.fast_path = fast_path;
    output.entry_path = result.entry_path;
    output.pkg_path = result.pkg_path;
    if (result.lock) {
      output.install_path = result.lock->install_dir();
      output.fetch_path = result.lock->fetch_dir();
      output.stage_path = result.lock->stage_dir();
    }
    output.lock_file = lock_file;
    std::string const kv = output.to_keyvalue();
    tui::print_stdout("%s", kv.c_str());
    throw std::runtime_error("cache-test: fail_before_complete requested");
  }

  // Mark complete if we got the lock
  if (result.lock) { result.lock->mark_install_complete(); }

  // Output result
  cache_test_result output{};
  output.locked = locked;
  output.fast_path = fast_path;
  output.entry_path = result.entry_path;
  output.pkg_path = result.pkg_path;
  if (result.lock) {
    output.install_path = result.lock->install_dir();
    output.fetch_path = result.lock->fetch_dir();
    output.stage_path = result.lock->stage_dir();
  }
  output.lock_file = lock_file;
  std::string const kv = output.to_keyvalue();
  tui::print_stdout("%s", kv.c_str());
}

}  // namespace envy
