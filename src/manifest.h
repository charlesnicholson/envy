#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace envy {

struct package_source {
  struct remote {
    std::string url;
    std::string sha256;
  };

  struct local {
    std::string file;
  };

  std::string recipe;  // "namespace.name@version"
  std::variant<remote, local> source;
  std::unordered_map<std::string, std::string> options;  // Key-value pairs
};

bool operator==(package_source const &lhs, package_source const &rhs);

using recipe_override = std::variant<package_source::remote, package_source::local>;

struct manifest {
  std::vector<package_source> packages;
  std::unordered_map<std::string, recipe_override> overrides;  // recipe -> override
  std::filesystem::path manifest_path;                         // Absolute path to envy.lua

  static std::optional<std::filesystem::path> discover();
  static manifest load(std::filesystem::path const &path);
};

}  // namespace envy
