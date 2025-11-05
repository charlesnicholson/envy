#pragma once

#include "fetch.h"
#include "util.h"

#include <filesystem>
#include <optional>
#include <string>

namespace envy {

void aws_init();
void aws_shutdown();

struct s3_download_request {
  std::string uri;
  std::filesystem::path destination;
  std::optional<std::string> region;
  fetch_progress_cb_t progress{};
};

std::filesystem::path aws_s3_download(s3_download_request const &request);

class aws_shutdown_guard : unmovable {
 public:
  aws_shutdown_guard() = default;
  ~aws_shutdown_guard();
};

}  // namespace envy
