#pragma once

#include "util.h"

#include <filesystem>
#include <vector>

namespace envy {

struct product_info;

void deploy_product_scripts(std::filesystem::path const &bin_dir,
                            std::vector<product_info> const &products,
                            bool strict,
                            std::vector<platform_id> const &platforms);

}  // namespace envy
