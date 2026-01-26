#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace envy {

enum class uri_scheme {
  S3,
  HTTP,
  HTTPS,
  FTP,
  FTPS,
  GIT,        // git:// or git+ssh:// (no SSL certs needed)
  GIT_HTTPS,  // https://...*.git (SSL certs needed)
  SSH,
  LOCAL_FILE_ABSOLUTE,
  LOCAL_FILE_RELATIVE,
  UNKNOWN
};

struct uri_info {
  uri_scheme scheme;
  std::string canonical;
};

uri_info uri_classify(std::string_view value);
std::string uri_extract_filename(std::string_view uri);

std::filesystem::path uri_resolve_local_file_relative(
    std::string_view local_file,
    std::optional<std::filesystem::path> const &anchor);

}  // namespace envy
