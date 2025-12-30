#pragma once

#include <string>

namespace envy {

struct pkg;

// Compute the rendered product value for a provider pkg.
// Returns pkg_path/value for cache-managed providers, raw value for user-managed.
std::string product_util_resolve(pkg *provider, std::string const &product_name);

}  // namespace envy
