#pragma once

#include "util.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace envy {

struct product_info;

inline constexpr int kProductScriptVersion = 1;

std::string deploy_stamp_product_script(std::string_view product_name,
                                        platform_id platform);

void deploy_product_scripts(std::filesystem::path const &bin_dir,
                            std::vector<product_info> const &products,
                            bool strict,
                            std::vector<platform_id> const &platforms);

// Shared tail of `sync` and `deploy`: refresh the bootstrap script for each
// platform, then either deploy product scripts (deploy_enabled) or explain how
// to turn deployment on. manifest_path names the file in the disabled hint.
void deploy_finalize(std::filesystem::path const &bin_dir,
                     std::optional<std::string> const &mirror,
                     std::vector<product_info> const &products,
                     std::vector<platform_id> const &platforms,
                     bool strict,
                     bool deploy_enabled,
                     std::filesystem::path const &manifest_path);

}  // namespace envy
