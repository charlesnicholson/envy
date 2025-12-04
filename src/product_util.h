#pragma once

#include <string>

namespace envy {

struct recipe;

// Compute the rendered product value for a provider recipe.
// Returns asset_path/value for cache-managed providers, raw value for user-managed.
std::string product_util_resolve(recipe *provider, std::string const &product_name);

}  // namespace envy
