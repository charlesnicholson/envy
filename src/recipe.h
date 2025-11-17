#pragma once

#include "cache.h"
#include "lua_util.h"
#include "recipe_key.h"
#include "recipe_spec.h"
#include "shell.h"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace envy {

// Plain data struct - engine orchestrates, phases operate on this
struct recipe {
  recipe_key key;
  recipe_spec spec;

  lua_state_ptr lua_state;
  cache::scoped_entry_lock::ptr_t lock;

  std::vector<std::string> declared_dependencies;
  std::unordered_map<std::string, recipe *> dependencies;

  std::string canonical_identity_hash;  // BLAKE3(format_key())
  std::filesystem::path asset_path;
  std::string result_hash;

  // Phase functions need access to these (not owned by recipe)
  cache *cache_ptr;
  default_shell_cfg_t const *default_shell_ptr;
};

}  // namespace envy
