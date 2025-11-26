#pragma once

#include "cache.h"
#include "recipe_key.h"
#include "recipe_phase.h"
#include "recipe_spec.h"
#include "shell.h"

#include "sol/sol.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace envy {

struct recipe {
  recipe_key key;
  recipe_spec const *spec;  // Non-ownership

  std::unique_ptr<sol::state> lua;
  cache::scoped_entry_lock::ptr_t lock;

  std::vector<std::string> declared_dependencies;

  // Owned specs for dependencies declared in this recipe's Lua file
  // (Root recipes reference specs in manifest; dependencies reference specs here)
  std::vector<recipe_spec> owned_dependency_specs;

  // Dependency info: maps identity -> (recipe pointer, needed_by phase)
  struct dependency_info {
    recipe *recipe_ptr;
    recipe_phase needed_by;  // Phase by which this dependency must be complete
  };
  std::unordered_map<std::string, dependency_info> dependencies;

  std::string canonical_identity_hash;  // BLAKE3(format_key())
  std::filesystem::path asset_path;
  std::string result_hash;

  // Phase functions need access to these (not owned by recipe)
  cache *cache_ptr;
  default_shell_cfg_t const *default_shell_ptr;
};

}  // namespace envy
