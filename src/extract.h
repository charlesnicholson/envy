#pragma once

#include "tui.h"

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

// Extract a single archive to destination
std::uint64_t extract(std::filesystem::path const &archive_path,
                      std::filesystem::path const &destination,
                      extract_options const &options = {});

// Check if path has archive extension
bool extract_is_archive_extension(std::filesystem::path const &path);

// Create tar.zst archive from source_dir contents, stored under prefix/ (e.g., "pkg/").
// Returns number of files archived.
std::uint64_t archive_create_tar_zst(std::filesystem::path const &output_path,
                                     std::filesystem::path const &source_dir,
                                     std::string const &prefix);

// Extract all archives in fetch_dir to dest_dir.
// If section != kInvalidSection, shows spinner during totals computation and progress bar
// during extraction. Pass kInvalidSection for silent extraction.
void extract_all_archives(std::filesystem::path const &fetch_dir,
                          std::filesystem::path const &dest_dir,
                          int strip_components,
                          std::string const &pkg_identity,
                          tui::section_handle section);

#ifdef ENVY_UNIT_TEST
// Exposed for unit tests only - computes totals by scanning archives
extract_totals compute_extract_totals(std::filesystem::path const &fetch_dir);
#endif

}  // namespace envy
