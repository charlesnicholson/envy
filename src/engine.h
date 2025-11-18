#pragma once

#include "cache.h"
#include "recipe_key.h"
#include "recipe_phase.h"
#include "recipe_spec.h"
#include "shell.h"
#include "util.h"

#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace envy {

struct manifest;
struct recipe;

struct recipe_result {
  std::string result_hash;  // BLAKE3(format_key()) or "programmatic" or empty (failed)
  std::filesystem::path asset_path;  // Path to asset/ dir (empty if programmatic/failed)
};

using recipe_result_map_t = std::unordered_map<std::string, recipe_result>;

class engine : unmovable {
 public:
  engine(cache &cache, default_shell_cfg_t default_shell);
  ~engine();

  recipe *ensure_recipe(recipe_spec const &spec);
  void register_alias(std::string const &alias, recipe_key const &key);

  recipe *find_exact(recipe_key const &key) const;
  std::vector<recipe *> find_matches(std::string_view query) const;

  // Phase coordination (thread-safe)
  void ensure_recipe_at_phase(recipe_key const &key, recipe_phase phase);
  void start_recipe_thread(recipe *r, recipe_phase initial_target);
  void wait_for_resolution_phase();
  void notify_phase_complete(recipe_key const &key, recipe_phase phase);
  void on_recipe_fetch_start();
  void on_recipe_fetch_complete();

  // High-level execution
  recipe_result_map_t run_full(std::vector<recipe_spec> const &roots);
  void resolve_graph(std::vector<recipe_spec> const &roots);

 private:
  cache &cache_;
  default_shell_cfg_t default_shell_;

  struct recipe_execution_ctx {
    std::thread worker;
    std::mutex mutex;
    std::condition_variable cv;
    std::atomic<recipe_phase> current_phase{ recipe_phase::none };  // Last completed phase
    std::atomic<recipe_phase> target_phase{ recipe_phase::none };
    std::atomic_bool failed{ false };
    std::atomic_bool started{ false };  // True if worker thread has been created

    void set_target_phase(recipe_phase target);
    void start(recipe *r, engine *eng);
  };

  void run_recipe_thread(recipe *r);  // Thread entry point

  std::unordered_map<recipe_key, std::unique_ptr<recipe>> recipes_;
  std::unordered_map<recipe_key, std::unique_ptr<recipe_execution_ctx>> execution_ctxs_;
  std::unordered_map<std::string, recipe_key> aliases_;
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::atomic_int pending_recipe_fetches_{ 0 };
};

}  // namespace envy
