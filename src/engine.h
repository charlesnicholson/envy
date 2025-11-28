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
  struct recipe_execution_ctx {  // Execution context for recipe threads
    std::thread worker;
    std::mutex mutex;
    std::condition_variable cv;
    std::atomic<recipe_phase> current_phase{ recipe_phase::none };  // Last completed phase
    std::atomic<recipe_phase> target_phase{ recipe_phase::none };
    std::atomic_bool failed{ false };
    std::atomic_bool started{ false };  // True if worker thread has been created

    std::vector<std::string> ancestor_chain;  // Per-thread for cycle detection
    std::string error_message;                // when failed=true (guarded by mutex)

    void set_target_phase(recipe_phase target);
    void start(recipe *r, engine *eng, std::vector<std::string> chain);
  };

  engine(cache &cache, default_shell_cfg_t default_shell);
  ~engine();

  recipe *ensure_recipe(recipe_spec const *spec);

  recipe *find_exact(recipe_key const &key) const;
  std::vector<recipe *> find_matches(std::string_view query) const;

  recipe_execution_ctx &get_execution_ctx(recipe *r);
  recipe_execution_ctx &get_execution_ctx(recipe_key const &key);

  // Phase coordination (thread-safe)
  void ensure_recipe_at_phase(recipe_key const &key, recipe_phase target_phase);
  void start_recipe_thread(recipe *r,
                           recipe_phase initial_target,
                           std::vector<std::string> ancestor_chain = {});
  void wait_for_resolution_phase();
  void notify_phase_complete();
  void on_recipe_fetch_start();
  void on_recipe_fetch_complete();

  // High-level execution
  recipe_result_map_t run_full(std::vector<recipe_spec const *> const &roots);
  void resolve_graph(std::vector<recipe_spec const *> const &roots);
  struct weak_resolution_result {
    size_t resolved{ 0 };
    size_t fallbacks_started{ 0 };
  };
  weak_resolution_result resolve_weak_references();

 private:
  cache &cache_;
  default_shell_cfg_t default_shell_;

  void notify_all_global_locked();
  void run_recipe_thread(recipe *r);  // Thread entry point
  void process_fetch_dependencies(recipe *r,
                                  std::vector<std::string> const &ancestor_chain);

  std::unordered_map<recipe_key, std::unique_ptr<recipe>> recipes_;
  std::unordered_map<recipe_key, std::unique_ptr<recipe_execution_ctx>> execution_ctxs_;
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::atomic_int pending_recipe_fetches_{ 0 };
};

// Validate that adding candidate_identity as a dependency doesn't create a cycle
// Checks for self-loops and cycles in ancestor_chain, throws on detection
// dependency_type used for error messages (e.g., "Dependency" or "Fetch dependency")
void validate_dependency_cycle(std::string const &candidate_identity,
                               std::vector<std::string> const &ancestor_chain,
                               std::string const &current_identity,
                               std::string const &dependency_type);

}  // namespace envy
