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
  GIT,
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

std::filesystem::path uri_resolve_local_file_relative(
    std::string_view local_file,
    std::optional<std::filesystem::path> const &anchor);

// Extract the filename component from a URI (everything after the last '/' before
// any query or fragment). Strips query strings (?) and fragments (#). Handles URL
// encoding transparently. Returns empty string if no filename component exists.
std::string uri_extract_filename(std::string_view uri);

}  // namespace envy
