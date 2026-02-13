#pragma once

#include "fetch.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace envy {

std::filesystem::path fetch_http_download(
    std::string_view url,
    std::filesystem::path const &destination,
    fetch_progress_cb_t const &progress,
    std::optional<std::string> const &post_data);

}  // namespace envy
