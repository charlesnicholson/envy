#pragma once

#include <filesystem>
#include <functional>
#include <optional>

namespace envy {

struct extract_progress {
  std::uint64_t bytes_processed{ 0 };
  std::optional<std::uint64_t> total_bytes;
  std::uint64_t files_processed{ 0 };
  std::optional<std::uint64_t> total_files;
  std::filesystem::path current_entry;
  bool is_regular_file{ false };
};

using extract_progress_cb_t = std::function<bool(extract_progress const &)>;

struct extract_totals {
  std::uint64_t bytes{ 0 };
  std::uint64_t files{ 0 };
};

struct extract_options {
  int strip_components{ 0 };
  extract_progress_cb_t progress;
};

std::uint64_t extract(std::filesystem::path const &archive_path,
                      std::filesystem::path const &destination,
                      extract_options const &options = {});

bool extract_is_archive_extension(std::filesystem::path const &path);

// Compute uncompressed totals for all regular files (plain + archives) in fetch_dir.
extract_totals compute_extract_totals(std::filesystem::path const &fetch_dir);

// Extract all archives in a directory
void extract_all_archives(std::filesystem::path const &fetch_dir,
                          std::filesystem::path const &dest_dir,
                          int strip_components,
                          extract_progress_cb_t progress = nullptr,
                          std::string const &pkg_identity = "",
                          std::function<void(std::string const &)> on_file = nullptr,
                          std::optional<extract_totals> totals_hint = std::nullopt);

}  // namespace envy
