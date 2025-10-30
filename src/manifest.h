#pragma once

#include "recipe.h"

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace envy {

// Override just specifies alternate source (no options)
using recipe_override =
    std::variant<recipe::cfg::remote_source, recipe::cfg::local_source>;

struct manifest {
  std::vector<recipe::cfg> packages;
  std::unordered_map<std::string, recipe_override> overrides;  // recipe identity -> override
  std::filesystem::path manifest_path;                         // Absolute path to envy.lua

  static std::optional<std::filesystem::path> discover();
  static manifest load(char const *script, std::filesystem::path const &manifest_path);
};

}  // namespace envy
