#pragma once

#include "cache.h"
#include "recipe_spec.h"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace envy {

struct manifest;

struct recipe_result {
  std::string result_hash;           // BLAKE3(format_key()) or "programmatic" or empty (failed)
  std::filesystem::path asset_path;  // Path to asset/ dir (empty if programmatic/failed)
};

using recipe_result_map_t = std::unordered_map<std::string, recipe_result>;

// Deprecated: Use recipe_result_map_t
using recipe_asset_hash_map_t = std::unordered_map<std::string, std::string>;

// Execute unified DAG: resolve recipes, fetch/build/install assets
recipe_result_map_t engine_run(std::vector<recipe_spec> const &roots,
                               cache &c,
                               manifest const &m);

}  // namespace envy
