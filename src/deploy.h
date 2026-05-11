#pragma once

#include "util.h"

#include <filesystem>
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

}  // namespace envy
