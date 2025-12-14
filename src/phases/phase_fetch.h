#pragma once

#include "fetch.h"

#include <filesystem>
#include <optional>
#include <string>

namespace envy {

class engine;
struct recipe;

void run_fetch_phase(recipe *r, engine &eng);

fetch_request url_to_fetch_request(std::string const &url,
                                   std::filesystem::path const &dest,
                                   std::optional<std::string> const &ref,
                                   std::string const &context);

}  // namespace envy
