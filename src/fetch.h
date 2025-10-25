#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "fetch_progress.h"

namespace envy {

enum class fetch_scheme { S3, HTTP, HTTPS, FTP, FTPS, GIT, SSH, LOCAL_FILE, UNKNOWN };

struct fetch_request {
  std::string source;
  std::filesystem::path destination;
  std::optional<std::filesystem::path> file_root;
  std::optional<std::string> region;
  fetch_progress_cb_t progress{};
};

struct fetch_result {
  fetch_scheme scheme;
  std::filesystem::path resolved_source;
  std::filesystem::path resolved_destination;
};

fetch_scheme fetch_classify(std::string_view uri);

fetch_result fetch(fetch_request const &request);

}  // namespace envy
