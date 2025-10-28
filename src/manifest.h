#pragma once

#include "recipe.h"

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace envy {

// Manifest-level recipe specification (identity + source + options)
struct recipe_spec {
  std::string identity;                                     // "namespace.name@version"
  recipe::source_t source;                                  // remote_source or local_source
  std::unordered_map<std::string, std::string> options;    // Key-value pairs
};

// Override just specifies alternate source (no options)
using recipe_override = std::variant<recipe::remote_source, recipe::local_source>;

struct manifest {
  std::vector<recipe_spec> packages;
  std::unordered_map<std::string, recipe_override> overrides;  // recipe identity -> override
  std::filesystem::path manifest_path;                         // Absolute path to envy.lua

  static std::optional<std::filesystem::path> discover();
  static manifest load(std::filesystem::path const &path);
};

}  // namespace envy
