#pragma once

#include "cache.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace envy::self_deploy {

// Create/open cache, self-deploy running binary + types, update latest, ensure hooks.
// `manifest_dir` anchors a relative `manifest_cache`; empty when no manifest is loaded.
std::unique_ptr<cache> ensure(std::optional<std::filesystem::path> const &cli_cache_root,
                              std::optional<std::string> const &manifest_cache,
                              std::filesystem::path const &manifest_dir);

}  // namespace envy::self_deploy
