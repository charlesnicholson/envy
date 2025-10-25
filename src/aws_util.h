#pragma once

#include <filesystem>
#include <string_view>

#include "fetch_progress.h"
#include "util.h"

namespace envy {

void aws_init();
void aws_shutdown();

void aws_s3_download(std::string_view s3_uri,
                     std::filesystem::path const &destination,
                     fetch_progress_cb_t const &progress = {});

class aws_shutdown_guard : unmovable {
 public:
  aws_shutdown_guard() = default;
  ~aws_shutdown_guard();
};

}  // namespace envy
