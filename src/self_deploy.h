#pragma once

#include "cache.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace envy::self_deploy {

// Create/open cache, self-deploy running binary + types, update latest, ensure hooks.
std::unique_ptr<cache> ensure(std::optional<std::filesystem::path> const &cli_cache_root,
                              std::optional<std::string> const &manifest_cache);

}  // namespace envy::self_deploy
