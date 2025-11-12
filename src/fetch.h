#pragma once

#include "uri.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace envy {

struct fetch_transfer_progress {
  std::uint64_t transferred{ 0 };
  std::optional<std::uint64_t> total;
};

struct fetch_git_progress {
  std::uint32_t total_objects{ 0 };
  std::uint32_t indexed_objects{ 0 };
  std::uint32_t received_objects{ 0 };
  std::uint32_t total_deltas{ 0 };
  std::uint32_t indexed_deltas{ 0 };
  std::uint64_t received_bytes{ 0 };
};

using fetch_progress_t = std::variant<fetch_transfer_progress, fetch_git_progress>;
using fetch_progress_cb_t = std::function<bool(fetch_progress_t const &)>;

// HTTP/HTTPS/FTP/FTPS fetch requests (no additional fields)
struct fetch_request_http {
  std::string source;
  std::filesystem::path destination;
  fetch_progress_cb_t progress{};
};

struct fetch_request_https {
  std::string source;
  std::filesystem::path destination;
  fetch_progress_cb_t progress{};
};

struct fetch_request_ftp {
  std::string source;
  std::filesystem::path destination;
  fetch_progress_cb_t progress{};
};

struct fetch_request_ftps {
  std::string source;
  std::filesystem::path destination;
  fetch_progress_cb_t progress{};
};

// S3 fetch request with region
struct fetch_request_s3 {
  std::string source;
  std::filesystem::path destination;
  fetch_progress_cb_t progress{};
  std::string region;
};

// Local file fetch request with file_root
struct fetch_request_file {
  std::string source;
  std::filesystem::path destination;
  fetch_progress_cb_t progress{};
  std::filesystem::path file_root;
};

// Git fetch request with ref (committish: tag, branch, or SHA)
// Note: Submodules are not yet supported
struct fetch_request_git {
  std::string source;
  std::filesystem::path destination;
  fetch_progress_cb_t progress{};
  std::string ref;
};

using fetch_request = std::variant<fetch_request_http,
                                   fetch_request_https,
                                   fetch_request_ftp,
                                   fetch_request_ftps,
                                   fetch_request_s3,
                                   fetch_request_file,
                                   fetch_request_git>;

struct fetch_result {
  uri_scheme scheme;
  std::filesystem::path resolved_source;
  std::filesystem::path resolved_destination;
};

using fetch_result_t = std::variant<fetch_result, std::string>;  // string on error

std::vector<fetch_result_t> fetch(std::vector<fetch_request> const &requests);

}  // namespace envy
