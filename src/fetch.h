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

using fetch_result_t = std::variant<fetch_result, std::string>;  // string on error

std::vector<fetch_result_t> fetch(std::vector<fetch_request> const &requests);

}  // namespace envy
