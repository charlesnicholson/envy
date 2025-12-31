#pragma once

#include "extract.h"
#include "fetch.h"
#include "tui.h"
#include "util.h"

#include <chrono>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace envy::tui_actions {

// Shell command execution progress tracker
// Lifetime: matches shell_run() blocking call
class run_progress {
 public:
  run_progress(tui::section_handle section,
               std::string const &pkg_identity,
               std::filesystem::path const &cache_root,
               product_map_t products = {});

  void on_command_start(std::string_view cmd);
  void on_output_line(std::string_view line);

 private:
  tui::section_handle section_;
  std::string label_;
  std::filesystem::path cache_root_;
  product_map_t products_;
  std::chrono::steady_clock::time_point start_time_;
  std::vector<std::string> lines_;
  std::string header_text_;
};

// Single-file extraction progress tracker
// Lifetime: matches extract() blocking call
class extract_progress_tracker {
 public:
  extract_progress_tracker(tui::section_handle section,
                           std::string const &pkg_identity,
                           std::string const &filename);

  bool operator()(extract_progress const &prog);

 private:
  tui::section_handle section_;
  std::string label_;
  std::string filename_;
  std::chrono::steady_clock::time_point start_time_;
};

// Multi-file extraction progress tracker with sub-sections
// Lifetime: matches extract_all_archives() blocking call
class extract_all_progress_tracker {
 public:
  extract_all_progress_tracker(tui::section_handle section,
                               std::string const &pkg_identity,
                               std::vector<std::string> const &filenames,
                               extract_totals const &totals);

  // Returns pair of callbacks: (progress_cb, on_file_cb)
  auto make_callbacks()
      -> std::pair<extract_progress_cb_t, std::function<void(std::string const &)>>;

 private:
  void set_frames(extract_progress const &prog);

  tui::section_handle section_;
  std::string label_;
  std::chrono::steady_clock::time_point start_time_;

  std::mutex mutex_;
  std::vector<tui::section_frame> children_;
  bool grouped_;
  extract_progress last_prog_;
};

// Download progress tracker (single file)
// Lifetime: matches fetch() blocking call
class fetch_progress_tracker {
 public:
  fetch_progress_tracker(tui::section_handle section,
                         std::string const &pkg_identity,
                         std::string const &url);

  bool operator()(fetch_progress_t const &prog);

 private:
  tui::section_handle section_;
  std::string label_;
  std::string url_;
  std::chrono::steady_clock::time_point start_time_;
};

// Multi-file download progress tracker with sub-sections
// Lifetime: matches fetch() blocking call for multiple downloads
class fetch_all_progress_tracker {
 public:
  fetch_all_progress_tracker(tui::section_handle section,
                             std::string const &pkg_identity,
                             std::vector<std::string> const &labels);

  fetch_progress_cb_t make_callback(std::size_t slot);

 private:
  struct git_state {
    double last_percent{ 0.0 };
    std::uint32_t max_total_objects{ 0 };
    std::uint32_t last_received_objects{ 0 };
    std::uint64_t last_bytes{ 0 };
  };

  void update_transfer(std::size_t slot, fetch_transfer_progress const &prog);
  void update_git(std::size_t slot, fetch_git_progress const &prog);
  void set_frame(std::size_t slot, tui::section_frame child_frame);

  tui::section_handle section_;
  std::string label_;
  std::mutex mutex_;
  std::vector<tui::section_frame> children_;
  std::vector<git_state> git_states_;
  bool grouped_;
};

}  // namespace envy::tui_actions
