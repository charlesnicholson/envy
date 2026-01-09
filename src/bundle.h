#pragma once

#include "pkg_cfg.h"

#include "sol/sol.hpp"

#include <filesystem>
#include <string>
#include <unordered_map>

namespace envy {

// Parsed in-memory representation of envy-bundle.lua
// Immutable after construction, shared across all specs from this bundle
struct bundle {
  std::string identity;                                // "namespace.name@revision"
  std::unordered_map<std::string, std::string> specs;  // spec identity -> relative path
  std::filesystem::path cache_path;  // e.g., ~/.envy/specs/acme.toolchain@v1/

  // Look up spec path within bundle. Returns empty path if not found.
  std::filesystem::path resolve_spec_path(std::string const &spec_identity) const;

  // Parse envy-bundle.lua from cache_path and construct bundle
  // Throws on parse error or validation failure
  static bundle from_path(std::filesystem::path const &cache_path);

  // Validate bundle (threaded):
  // - All spec files exist at declared paths
  // - All spec files execute successfully in Lua
  // - All spec files have IDENTITY matching the SPECS table key
  // Throws with detailed error message on failure
  void validate() const;

  // Parse BUNDLES table from manifest into alias -> fetch config map
  // Returns empty map if bundles_obj is nil or missing
  // Throws on invalid format
  static std::unordered_map<std::string, pkg_cfg::bundle_source> parse_aliases(
      sol::object const &bundles_obj,
      std::filesystem::path const &base_path);

  // Parse inline bundle = {...} declaration directly to bundle_source
  // Throws on invalid format
  static pkg_cfg::bundle_source parse_inline(sol::table const &table,
                                             std::filesystem::path const &base_path);

  // Configure an existing lua state's package.path to include this bundle's root.
  // Call this before loading any spec files from the bundle.
  void configure_package_path(sol::state &lua) const;
};

}  // namespace envy
