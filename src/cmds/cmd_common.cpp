#include "cmd_common.h"

#include "manifest.h"
#include "platform.h"

#include <stdexcept>

namespace envy {

std::unique_ptr<manifest> load_manifest_or_throw(
    std::optional<std::filesystem::path> const &manifest_path) {
  auto const path{ manifest::find_manifest_path(manifest_path) };
  auto m{ manifest::load(path) };
  if (!m) { throw std::runtime_error("could not load manifest"); }
  return m;
}

std::filesystem::path resolve_cache_root(
    std::optional<std::filesystem::path> const &cache_root) {
  if (cache_root) { return *cache_root; }

  auto default_cache_root{ platform::get_default_cache_root() };
  if (!default_cache_root) { throw std::runtime_error("could not determine cache root"); }
  return *default_cache_root;
}

}  // namespace envy
