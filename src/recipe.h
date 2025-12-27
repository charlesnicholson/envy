#pragma once

#include "cache.h"
#include "recipe_key.h"
#include "recipe_phase.h"
#include "recipe_spec.h"
#include "shell.h"
#include "sol_util.h"
#include "tui.h"

#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace envy {

enum class recipe_type;
struct recipe_execution_ctx;

struct recipe {
  struct dependency_info {  // recipe and phase by which dependency must be complete
    recipe *r;
    recipe_phase needed_by;
  };

  struct product_dependency {  // product name, required phase, and resolved provider
    std::string name;
    recipe_phase needed_by{ recipe_phase::asset_build };
    recipe *provider{ nullptr };
    std::string constraint_identity;
  };

  struct weak_reference {  // unresolved dependency, may match multiple recipes or fallback
    std::string query;
    recipe_spec const *fallback{ nullptr };
    recipe_phase needed_by{ recipe_phase::asset_build };
    recipe *resolved{ nullptr };
    bool is_product{ false };
    std::string constraint_identity;
  };

  // Immutable after construction
  recipe_key const key;
  recipe_spec const *const spec;
  cache *const cache_ptr;
  default_shell_cfg_t const *const default_shell_ptr;
  tui::section_handle const tui_section;

  recipe_execution_ctx *exec_ctx{ nullptr };  // assigned by engine

  sol_state_ptr lua;
  mutable std::mutex lua_mutex;
  cache::scoped_entry_lock::ptr_t lock;

  // Single-writer fields (set during specific phases, read after)
  std::string canonical_identity_hash;
  std::filesystem::path asset_path;
  std::optional<std::filesystem::path> recipe_file_path;
  std::string result_hash;
  recipe_type type;

  // Dependency state
  std::vector<std::string> declared_dependencies;
  std::vector<recipe_spec *> owned_dependency_specs;
  std::unordered_map<std::string, dependency_info> dependencies;
  std::unordered_map<std::string, product_dependency> product_dependencies;
  std::vector<weak_reference> weak_references;
  std::unordered_map<std::string, std::string> products;
  std::vector<std::string> resolved_weak_dependency_keys;
};

}  // namespace envy
