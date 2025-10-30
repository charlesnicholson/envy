#pragma once

#include "recipe.h"

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace envy {

struct manifest {
  std::vector<recipe::cfg> packages;
  std::filesystem::path manifest_path;  // Absolute path to envy.lua

  static std::optional<std::filesystem::path> discover();
  static manifest load(char const *script, std::filesystem::path const &manifest_path);
};

}  // namespace envy
