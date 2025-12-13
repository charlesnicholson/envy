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

struct extract_options {
  int strip_components{ 0 };
  extract_progress_cb_t progress;
};

std::uint64_t extract(std::filesystem::path const &archive_path,
                      std::filesystem::path const &destination,
                      extract_options const &options = {});

bool extract_is_archive_extension(std::filesystem::path const &path);

// Extract all archives in a directory
void extract_all_archives(std::filesystem::path const &fetch_dir,
                          std::filesystem::path const &dest_dir,
                          int strip_components,
                          extract_progress_cb_t progress = nullptr,
                          std::string const &recipe_identity = "");

}  // namespace envy
