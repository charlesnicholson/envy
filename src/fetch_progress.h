#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <variant>

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

}  // namespace envy
