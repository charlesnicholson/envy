#pragma once

#include <filesystem>
#include <functional>
#include <optional>

namespace envy {

struct extract_progress {
  std::uint64_t bytes_processed{ 0 };
  std::optional<std::uint64_t> total_bytes;
  std::filesystem::path current_entry;
};

using extract_progress_cb_t = std::function<bool(extract_progress const &)>;

std::uint64_t extract(std::filesystem::path const &archive_path,
                      std::filesystem::path const &destination,
                      extract_progress_cb_t const &progress = {});

}  // namespace envy
