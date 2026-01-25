#pragma once

#include "bundle.h"
#include "cache.h"
#include "pkg_cfg.h"
#include "pkg_key.h"
#include "pkg_phase.h"
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
#include <unordered_set>
#include <vector>

namespace envy {

struct manifest;
struct pkg;

enum class pkg_type {
  UNKNOWN,        // Not yet determined or failed
  CACHE_MANAGED,  // Package produces cached artifacts (has fetch)
  USER_MANAGED,   // Package managed by user (has check/install, no cache artifacts)
  BUNDLE_ONLY     // Pure bundle dependency (no spec, just bundle for envy.loadenv_spec())
};

struct pkg_execution_ctx {
  std::thread worker;
  std::mutex mutex;
  std::condition_variable cv;
  std::atomic<pkg_phase> current_phase{ pkg_phase::none };  // executing or pending
  std::atomic<pkg_phase> target_phase{ pkg_phase::none };
  std::atomic_bool failed{ false };
  std::atomic_bool started{ false };  // True if worker thread has been created
  std::atomic_bool spec_fetch_completed{ false };  // True after spec_fetch completes

  std::vector<std::string> ancestor_chain;  // Per-thread for cycle detection
  std::string error_message;                // when failed=true (guarded by mutex)

  void set_target_phase(pkg_phase target);
  void start(struct pkg *p, class engine *eng, std::vector<std::string> chain);
};

struct pkg_result {
  pkg_type type;
  std::string result_hash;  // BLAKE3(format_key()) if cache-managed, empty otherwise
  std::filesystem::path pkg_path;  // Path to pkg/ dir (empty if user-managed/unknown)
};

using pkg_result_map_t = std::unordered_map<std::string, pkg_result>;

struct product_info {
  std::string product_name;
  std::string value;
  std::string provider_canonical;  // Full canonical identity with options
  pkg_type type;
  std::filesystem::path pkg_path;
  bool script = true;
};

class engine : unmovable {
 public:
  engine(cache &cache, manifest const *manifest = nullptr);
  ~engine();

  pkg *ensure_pkg(pkg_cfg const *cfg);

  pkg *find_exact(pkg_key const &key) const;
  std::vector<pkg *> find_matches(std::string_view query) const;
  pkg *find_product_provider(std::string const &product_name) const;
  std::vector<product_info> collect_all_products() const;

  pkg_execution_ctx &get_execution_ctx(pkg *p);
  pkg_execution_ctx &get_execution_ctx(pkg_key const &key);
  pkg_execution_ctx const &get_execution_ctx(pkg_key const &key) const;

  // Phase coordination (thread-safe)
  void ensure_pkg_at_phase(pkg_key const &key, pkg_phase target_phase);
  void start_pkg_thread(pkg *p,
                        pkg_phase initial_target,
                        std::vector<std::string> ancestor_chain = {});
  void wait_for_resolution_phase();
  void notify_phase_complete();
  void on_spec_fetch_start();
  void on_spec_fetch_complete(std::string const &pkg_identity);

  // High-level execution
  pkg_result_map_t run_full(std::vector<pkg_cfg const *> const &roots);

  void resolve_graph(std::vector<pkg_cfg const *> const &roots);

  struct weak_resolution_result {
    size_t resolved{ 0 };
    size_t fallbacks_started{ 0 };
    std::vector<std::string> missing_without_fallback;
  };
  weak_resolution_result resolve_weak_references();

  void extend_dependencies_to_completion(pkg *p);

  std::filesystem::path const &cache_root() const;

  // Bundle registry management
  // Register a fetched bundle; returns existing if already registered
  bundle *register_bundle(std::string const &identity,
                          std::unordered_map<std::string, std::string> specs,
                          std::filesystem::path cache_path);

  bundle *find_bundle(std::string const &identity) const;

  manifest const *get_manifest() const { return manifest_; }

#ifdef ENVY_UNIT_TEST
  pkg_phase get_pkg_target_phase(pkg_key const &key) const;
#endif

 private:
  void fail_all_contexts();

  cache &cache_;
  default_shell_cfg_t default_shell_;
  manifest const *manifest_{ nullptr };  // For bundle fetch function lookup

  void notify_all_global_locked();
  void run_pkg_thread(pkg *p);  // Thread entry point
  void process_fetch_dependencies(pkg *p, std::vector<std::string> const &ancestor_chain);
  void update_product_registry();
  void validate_product_fallbacks();
  bool pkg_provides_product_transitively(pkg *p, std::string const &product_name) const;
  void extend_dependencies_recursive(pkg *p, std::unordered_set<pkg_key> &visited);

  friend struct pkg_execution_ctx;  // Allows worker threads to call run_pkg_thread

  std::unordered_map<pkg_key, std::unique_ptr<pkg>> packages_;
  std::unordered_map<pkg_key, std::unique_ptr<pkg_execution_ctx>> execution_ctxs_;
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::atomic_int pending_spec_fetches_{ 0 };

  // Product registry: maps product name → provider package (built during resolution)
  std::unordered_map<std::string, pkg *> product_registry_;

  // Bundle registry: maps bundle identity → bundle (populated during fetch)
  std::unordered_map<std::string, std::unique_ptr<bundle>> bundle_registry_;
};

// Validate that adding candidate_identity as a dependency doesn't create a cycle
// Checks for self-loops and cycles in ancestor_chain, throws on detection
// dependency_type used for error messages (e.g., "Dependency" or "Fetch dependency")
void engine_validate_dependency_cycle(std::string const &candidate_identity,
                                      std::vector<std::string> const &ancestor_chain,
                                      std::string const &current_identity,
                                      std::string const &dependency_type);

}  // namespace envy
