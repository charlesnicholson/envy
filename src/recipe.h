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
  recipe_key key;
  recipe_spec const *spec;                           // Non-ownership
  struct recipe_execution_ctx *exec_ctx{ nullptr };  // Set by engine

  sol_state_ptr lua;
  mutable std::mutex lua_mutex;  // Protects child access to parent's lua (custom fetch)
  cache::scoped_entry_lock::ptr_t lock;

  std::vector<std::string> declared_dependencies;

  // Owned specs for dependencies declared in this recipe's Lua file
  // (Root recipes reference specs in manifest; dependencies reference specs here)
  std::vector<recipe_spec *> owned_dependency_specs;

  // Dependency info: maps identity -> (recipe pointer, needed_by phase)
  struct dependency_info {
    recipe *recipe_ptr;
    recipe_phase needed_by;  // Phase by which this dependency must be complete
  };
  std::unordered_map<std::string, dependency_info> dependencies;

  struct product_dependency {
    std::string name;
    recipe_phase needed_by{ recipe_phase::asset_build };
    recipe *provider{ nullptr };  // strong deps immediately, weak deps after resolution
    std::string constraint_identity;  // Optional required provider identity (empty if none)
  };
  std::unordered_map<std::string, product_dependency> product_dependencies;

  struct weak_reference {
    std::string query;                       // Partial identity query OR product name
    recipe_spec const *fallback{ nullptr };  // weak dep, null for ref-only
    recipe_phase needed_by{ recipe_phase::asset_build };
    recipe *resolved{ nullptr };      // Filled in during resolution
    bool is_product{ false };         // True if query is a product name
    std::string constraint_identity;  // Required recipe identity (empty if unconstrained)
  };
  std::vector<weak_reference> weak_references;

  // Products map: product name -> relative path (or raw value for user-managed recipes)
  std::unordered_map<std::string, std::string> products;

  // Cached resolved weak dependency keys for hash computation (populated after resolution)
  std::vector<std::string> resolved_weak_dependency_keys;

  std::string canonical_identity_hash;  // BLAKE3(format_key())
  std::filesystem::path asset_path;
  std::optional<std::filesystem::path> recipe_file_path;  // Actual recipe.lua file loaded
  std::string result_hash;
  recipe_type type;  // Determined during recipe_fetch phase

  // Phase functions need access to these (not owned by recipe)
  cache *cache_ptr;
  default_shell_cfg_t const *default_shell_ptr;

  tui::section_handle tui_section{ 0 };
};

}  // namespace envy
