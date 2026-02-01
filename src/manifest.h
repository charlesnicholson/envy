#pragma once

#include "pkg_cfg.h"
#include "shell.h"
#include "sol_util.h"
#include "util.h"

#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace envy {

// @envy metadata parsed from comment headers in manifest
struct envy_meta {
  std::optional<std::string> version;  // @envy version "x.y.z"
  std::optional<std::string> cache;    // @envy cache-posix or cache-win
  std::optional<std::string> mirror;   // @envy mirror "https://..."
  std::optional<std::string> bin;      // @envy bin "relative/path/to/bin"
  std::optional<bool> deploy;          // @envy deploy "true"/"false"
  std::optional<bool> root;            // @envy root "true"/"false"
};

// Parse @envy metadata from manifest content
envy_meta parse_envy_meta(std::string_view content);

struct manifest : unmovable {
  std::vector<pkg_cfg *> packages;
  std::filesystem::path manifest_path;
  envy_meta meta;

  manifest() = default;

  // Find manifest path: use provided path if given, otherwise discover from current
  // directory. Returns absolute path or throws if not found
  static std::filesystem::path find_manifest_path(
      std::optional<std::filesystem::path> const &explicit_path);

  static std::optional<std::filesystem::path> discover();

  static std::unique_ptr<manifest> load(std::filesystem::path const &manifest_path);
  static std::unique_ptr<manifest> load(std::vector<unsigned char> const &content,
                                        std::filesystem::path const &manifest_path);
  static std::unique_ptr<manifest> load(char const *script,
                                        std::filesystem::path const &manifest_path);

  // Get DEFAULT_SHELL global type and value
  // Returns nullopt if no DEFAULT_SHELL specified
  default_shell_cfg_t get_default_shell() const;

  // Execute bundle custom fetch function from BUNDLES table
  // Sets up phase context, executes fetch function, cleans up
  // Returns nullopt if bundle not found or has no custom fetch
  // Returns error message string on failure, nullopt on success
  std::optional<std::string> run_bundle_fetch(std::string const &bundle_identity,
                                              void *phase_ctx,
                                              std::filesystem::path const &tmp_dir) const;

 private:
  mutable std::mutex lua_mutex_;  // Protects lua_ access from concurrent threads
  sol_state_ptr lua_;
};

}  // namespace envy
