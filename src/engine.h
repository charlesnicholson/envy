#pragma once

#include "cache.h"
#include "recipe_spec.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace envy {

struct manifest;

using recipe_asset_hash_map_t = std::unordered_map<std::string, std::string>;

// Execute unified DAG: resolve recipes, fetch/build/install assets
recipe_asset_hash_map_t engine_run(std::vector<recipe_spec> const &roots,
                                   cache &c,
                                   manifest const &m);

}  // namespace envy
