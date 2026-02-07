#pragma once

#include "fetch.h"

#include <filesystem>
#include <optional>
#include <string>

namespace envy {

class engine;
struct pkg;

void run_fetch_phase(pkg *p, engine &eng);

fetch_request url_to_fetch_request(
    std::string const &url,
    std::filesystem::path const &dest,
    std::optional<std::string> const &ref,
    std::optional<std::string> const &post_data,
    std::string const &context,
    std::optional<std::filesystem::path> const &file_root = {});

}  // namespace envy
