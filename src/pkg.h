#pragma once

#include "cache.h"
#include "pkg_cfg.h"
#include "pkg_key.h"
#include "pkg_phase.h"
#include "shell.h"
#include "sol_util.h"
#include "tui.h"

#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace envy {

enum class pkg_type;
struct pkg_execution_ctx;

struct product_entry {
  std::string value;
  bool script = true;
  std::vector<std::string> platforms;  // empty = inherit from package
};

struct pkg {
  struct dependency_info {  // pkg and phase by which dependency must be complete
    pkg *p;
    pkg_phase needed_by;
  };

  struct product_dependency {  // product name, required phase, and resolved provider
    std::string name;
    pkg_phase needed_by{ pkg_phase::pkg_build };
    pkg *provider{ nullptr };
    std::string constraint_identity;
  };

  struct weak_reference {  // unresolved dependency, may match multiple packages or
                           // fallback
    std::string query;
    pkg_cfg const *fallback{ nullptr };
    pkg_phase needed_by{ pkg_phase::pkg_build };
    pkg *resolved{ nullptr };
    bool is_product{ false };
    std::string constraint_identity;
  };

  // Immutable after construction
  pkg_key const key;
  pkg_cfg const *const cfg;
  cache *const cache_ptr;
  default_shell_cfg_t const *const default_shell_ptr;
  tui::section_handle const tui_section;

  pkg_execution_ctx *exec_ctx{ nullptr };  // assigned by engine

  sol_state_guard lua;
  cache::scoped_entry_lock::ptr_t lock;

  // Single-writer fields (set during specific phases, read after)
  std::string canonical_identity_hash;
  std::filesystem::path pkg_path;
  std::optional<std::filesystem::path> spec_file_path;
  std::string result_hash;
  pkg_type type;
  int schema{ 0 };

  // SETUP pairs declared by the spec: name → per-pair platform constraints
  // (empty = all). Sorted map gives deterministic execution order. Written once
  // during spec_fetch, read by the setup phase.
  std::map<std::string, std::vector<std::string>> setup_pairs;

  // Dependency state — deps_mutex guards every field below. The engine's resolution
  // loop mutates these maps while worker threads traverse them. Lock one node at a
  // time; snapshot before recursing or blocking so no two pkg locks nest.
  mutable std::mutex deps_mutex;
  std::vector<std::string> declared_dependencies;
  std::vector<pkg_cfg *> owned_dependency_cfgs;
  std::unordered_map<std::string, dependency_info> dependencies;
  std::unordered_map<std::string, product_dependency> product_dependencies;
  std::vector<weak_reference> weak_references;
  std::unordered_map<std::string, product_entry> products;
  std::vector<std::string> resolved_platforms;
  std::vector<std::string> resolved_weak_dependency_keys;

  // SETUP selection, merged across referring cfgs (manifest entries only may
  // carry `setup`). setup_default means some referrer omitted `setup`: the
  // package-type default applies (user-managed: all pairs; cache-managed: none).
  std::unordered_set<std::string> setup_selected;
  bool setup_default{ false };
};

}  // namespace envy
