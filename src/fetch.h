#pragma once

#include "fetch_progress.h"
#include "uri.h"

#include <filesystem>
#include <optional>
#include <string>

namespace envy {

struct fetch_request {
  std::string source;
  std::filesystem::path destination;
  std::optional<std::filesystem::path> file_root;
  std::optional<std::string> region;
  fetch_progress_cb_t progress{};
};

struct fetch_result {
  uri_scheme scheme;
  std::filesystem::path resolved_source;
  std::filesystem::path resolved_destination;
};

fetch_result fetch(fetch_request const &request);

}  // namespace envy
