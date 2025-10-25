#pragma once

#include "fetch_progress.h"

#include <filesystem>
#include <string_view>

namespace envy {
std::filesystem::path libcurl_download(std::string_view url,
                                       std::filesystem::path const &destination,
                                       fetch_progress_cb_t const &progress = {});
}  // namespace envy
