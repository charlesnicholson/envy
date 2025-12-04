#pragma once

#include <filesystem>
#include <memory>
#include <optional>

namespace envy {

struct manifest;

std::unique_ptr<manifest> load_manifest_or_throw(
    std::optional<std::filesystem::path> const &manifest_path);
std::filesystem::path resolve_cache_root(
    std::optional<std::filesystem::path> const &cache_root);

}  // namespace envy
