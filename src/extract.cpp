#include "extract.h"

#include "trace.h"
#include "tui.h"
#include "util.h"

#include "archive.h"
#include "archive_entry.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

namespace envy {
namespace {

struct archive_reader : unmovable {
  archive_reader() : handle(archive_read_new()) {
    if (!handle) { throw std::runtime_error("archive_read_new failed"); }
    archive_read_support_filter_all(handle);
    archive_read_support_format_all(handle);
  }

  ~archive_reader() {
    if (handle) {
      archive_read_close(handle);
      archive_read_free(handle);
    }
  }

  archive *handle{ nullptr };
};

struct archive_writer : unmovable {
  archive_writer() : handle(archive_write_disk_new()) {
    if (!handle) { throw std::runtime_error("archive_write_disk_new failed"); }
    archive_write_disk_set_options(handle,
                                   ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_PERM |
                                       ARCHIVE_EXTRACT_ACL | ARCHIVE_EXTRACT_FFLAGS);
    archive_write_disk_set_standard_lookup(handle);
  }

  ~archive_writer() {
    if (handle) {
      archive_write_close(handle);
      archive_write_free(handle);
    }
  }

  archive *handle{ nullptr };
};

void ensure_directory(std::filesystem::path const &path) {
  auto const dir{ path.parent_path() };
  if (dir.empty()) { return; }
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  if (ec) {
    throw std::runtime_error(std::string("Failed to create directory ") + dir.string() +
                             ": " + ec.message());
  }
}

bool is_safe_archive_path(char const *path) {
  if (!path || path[0] == '\0') { return false; }
  if (path[0] == '/' || path[0] == '\\') { return false; }
#ifdef _WIN32
  if (std::isalpha(static_cast<unsigned char>(path[0])) && path[1] == ':') {
    return false;
  }
#endif
  std::string_view sv{ path };
  // Reject paths containing ".." components
  for (std::size_t pos{ 0 }; pos < sv.size();) {
    auto const sep{ sv.find_first_of("/\\", pos) };
    auto const component{ sv.substr(pos,
                                    sep == std::string_view::npos ? sep : sep - pos) };
    if (component == "..") { return false; }
    pos = (sep == std::string_view::npos) ? sv.size() : sep + 1;
  }
  return true;
}

std::optional<std::string> strip_path_components(char const *path, int strip_count) {
  if (!path) { return std::nullopt; }
  if (strip_count <= 0) { return std::string(path); }

  char const *p{ path };
  int components_stripped{ 0 };

  while (*p == '/') { ++p; }

  while (components_stripped < strip_count) {
    if (*p == '\0') { return std::nullopt; }
    if (*p == '/') {
      ++components_stripped;
      while (*p == '/') { ++p; }
    } else {
      ++p;
    }
  }

  if (*p == '\0') { return std::nullopt; }
  return std::string(p);
}

// Build list of files to extract from fetch_dir
std::vector<std::string> collect_extract_items(std::filesystem::path const &fetch_dir) {
  std::vector<std::string> items;
  if (!std::filesystem::exists(fetch_dir)) { return items; }

  for (auto const &entry : std::filesystem::directory_iterator(fetch_dir)) {
    if (!entry.is_regular_file()) { continue; }
    if (entry.path().filename() == "envy-complete") { continue; }
    items.push_back(entry.path().filename().string());
  }
  return items;
}

// TUI progress state for extract_all_archives
struct extract_tui_state {
  tui::section_handle section;
  std::string label;
  std::vector<tui::section_frame> children;
  bool grouped;
  extract_totals totals;
  std::uint64_t files_processed{ 0 };
  std::uint64_t bytes_processed{ 0 };
  std::filesystem::path last_file_seen;
  std::optional<std::size_t> current_file_idx;

  extract_tui_state(tui::section_handle s,
                    std::string const &pkg_identity,
                    std::vector<std::string> const &filenames,
                    extract_totals const &t)
      : section{ s },
        label{ "[" + pkg_identity + "]" },
        grouped{ filenames.size() > 1 },
        totals{ t } {
    children.reserve(filenames.size());
    for (auto const &name : filenames) {
      children.push_back(
          tui::section_frame{ .label = name,
                              .content = tui::static_text_data{ .text = "pending" } });
    }
  }

  void set_spinner(std::string const &text) {
    tui::section_set_content(
        section,
        tui::section_frame{ .label = label,
                            .content = tui::spinner_data{
                                .text = text,
                                .start_time = std::chrono::steady_clock::now() } });
  }

  void update_progress() {
    double percent{ 0.0 };
    if (totals.files > 0) {
      percent = (files_processed / static_cast<double>(totals.files)) * 100.0;
    } else if (totals.bytes > 0) {
      percent = (bytes_processed / static_cast<double>(totals.bytes)) * 100.0;
    }
    if (percent > 100.0) { percent = 100.0; }

    std::ostringstream status;
    status << files_processed;
    if (totals.files > 0) { status << "/" << totals.files; }
    status << " files";
    if (totals.bytes > 0) {
      status << " " << util_format_bytes(bytes_processed) << "/"
             << util_format_bytes(totals.bytes);
    } else if (bytes_processed > 0) {
      status << " " << util_format_bytes(bytes_processed);
    }

    if (grouped) {
      tui::section_set_content(
          section,
          tui::section_frame{
              .label = label,
              .content = tui::progress_data{ .percent = percent, .status = status.str() },
              .children = children });
    } else {
      std::string item{ children.empty() ? "" : children.front().label };
      tui::section_set_content(
          section,
          tui::section_frame{
              .label = label,
              .content = tui::progress_data{
                  .percent = percent,
                  .status = item.empty() ? status.str() : (status.str() + " " + item) } });
    }
  }

  void on_file_start(std::string const &name) {
    // Mark previous file as done
    if (current_file_idx && *current_file_idx < children.size()) {
      children[*current_file_idx].content = tui::static_text_data{ .text = "done" };
    }

    // Find and mark current file as in-progress
    if (auto it{ std::find_if(children.begin(),
                              children.end(),
                              [&](auto const &c) { return c.label == name; }) };
        it != children.end()) {
      auto idx{ static_cast<std::size_t>(std::distance(children.begin(), it)) };
      current_file_idx = idx;
      children[idx].content =
          tui::spinner_data{ .text = "extracting",
                             .start_time = std::chrono::steady_clock::now() };
    }
    update_progress();
  }

  bool on_progress(std::uint64_t bytes,
                   std::filesystem::path const &entry,
                   bool is_regular_file) {
    bytes_processed = bytes;
    if (is_regular_file && entry != last_file_seen) {
      ++files_processed;
      last_file_seen = entry;
    }
    update_progress();
    return true;
  }
};

}  // namespace

std::uint64_t archive_create_tar_zst(std::filesystem::path const &output_path,
                                     std::filesystem::path const &source_dir,
                                     std::string const &prefix) {
  archive *a{ archive_write_new() };
  if (!a) { throw std::runtime_error("archive_write_new failed"); }

  archive_write_set_format_pax_restricted(a);
  archive_write_add_filter_zstd(a);

  ensure_directory(output_path);

  if (archive_write_open_filename(a, output_path.string().c_str()) != ARCHIVE_OK) {
    std::string msg{ std::string("Failed to open output: ") + archive_error_string(a) };
    archive_write_free(a);
    throw std::runtime_error(msg);
  }

  archive_entry *entry{ archive_entry_new() };
  std::uint64_t files_archived{ 0 };
  std::vector<char> buffer(1024 * 1024);

  for (auto const &dir_entry : std::filesystem::recursive_directory_iterator(source_dir)) {
    std::filesystem::path const rel{ dir_entry.path().lexically_relative(source_dir) };
    std::string const archived_path{ prefix + "/" + rel.generic_string() };

    archive_entry_clear(entry);
    archive_entry_set_pathname(entry, archived_path.c_str());

    if (dir_entry.is_symlink()) {
      archive_entry_set_filetype(entry, AE_IFLNK);
      auto const target{ std::filesystem::read_symlink(dir_entry.path()) };
      archive_entry_set_symlink(entry, target.string().c_str());
      archive_entry_set_size(entry, 0);
    } else if (dir_entry.is_directory()) {
      archive_entry_set_filetype(entry, AE_IFDIR);
      archive_entry_set_size(entry, 0);
    } else if (dir_entry.is_regular_file()) {
      archive_entry_set_filetype(entry, AE_IFREG);
      auto const sz{ std::filesystem::file_size(dir_entry.path()) };
      archive_entry_set_size(entry, static_cast<la_int64_t>(sz));
    } else {
      continue;
    }

    // Preserve permissions
    std::error_code ec;
    auto const perms{ std::filesystem::status(dir_entry.path(), ec).permissions() };
    if (!ec) { archive_entry_set_perm(entry, static_cast<__LA_MODE_T>(perms)); }

    if (archive_write_header(a, entry) != ARCHIVE_OK) {
      std::string msg{ std::string("Failed to write header: ") + archive_error_string(a) };
      archive_entry_free(entry);
      archive_write_close(a);
      archive_write_free(a);
      throw std::runtime_error(msg);
    }

    if (dir_entry.is_regular_file()) {
      std::ifstream in{ dir_entry.path(), std::ios::binary };
      if (!in) {
        archive_entry_free(entry);
        archive_write_close(a);
        archive_write_free(a);
        throw std::runtime_error("Failed to open file: " + dir_entry.path().string());
      }

      while (in) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        auto const bytes_read{ in.gcount() };
        if (bytes_read > 0) {
          if (archive_write_data(a, buffer.data(), static_cast<size_t>(bytes_read)) < 0) {
            std::string msg{ std::string("Failed to write data: ") +
                             archive_error_string(a) };
            archive_entry_free(entry);
            archive_write_close(a);
            archive_write_free(a);
            throw std::runtime_error(msg);
          }
        }
      }
      ++files_archived;
    }
  }

  archive_entry_free(entry);
  archive_write_close(a);
  archive_write_free(a);
  return files_archived;
}

std::uint64_t extract(std::filesystem::path const &archive_path,
                      std::filesystem::path const &destination,
                      extract_options const &options) {
  archive_reader reader;
  archive_writer writer;

  if (archive_read_open_filename(reader.handle, archive_path.string().c_str(), 10240) !=
      ARCHIVE_OK) {
    throw std::runtime_error(std::string("Failed to open archive: ") +
                             archive_error_string(reader.handle));
  }

  archive_entry *entry{ nullptr };
  std::uint64_t processed{ 0 };
  std::uint64_t files_extracted{ 0 };

  while (true) {
    int const r{ archive_read_next_header(reader.handle, &entry) };
    if (r == ARCHIVE_EOF) { break; }

    if (r != ARCHIVE_OK) {
      throw std::runtime_error(std::string("Failed to read archive header: ") +
                               archive_error_string(reader.handle));
    }

    char const *entry_path{ archive_entry_pathname(entry) };
    if (!entry_path) { throw std::runtime_error("Archive entry has null pathname"); }

    bool const is_regular_file{ archive_entry_filetype(entry) == AE_IFREG };

    std::string stripped_path;
    if (options.strip_components > 0) {
      auto stripped{ strip_path_components(entry_path, options.strip_components) };
      if (!stripped) { continue; }
      stripped_path = *stripped;
      entry_path = stripped_path.c_str();
    }

    if (!is_safe_archive_path(entry_path)) {
      throw std::runtime_error(std::string("extract: unsafe archive entry path: ") +
                               entry_path);
    }

    std::filesystem::path const full_path{ destination / entry_path };
    ensure_directory(full_path);

    {
      std::string const full_path_str{ full_path.string() };
      archive_entry_copy_pathname(entry, full_path_str.c_str());
    }

    if (char const *hardlink{ archive_entry_hardlink(entry) }) {
      std::string hardlink_str{ hardlink };
      if (options.strip_components > 0) {
        auto stripped{ strip_path_components(hardlink, options.strip_components) };
        if (stripped) { hardlink_str = *stripped; }
      }
      std::string const hardlink_full{ (destination / hardlink_str).string() };
      archive_entry_copy_hardlink(entry, hardlink_full.c_str());
    }

    if (options.progress &&
        !options.progress(extract_progress{ .bytes_processed = processed,
                                            .total_bytes = std::nullopt,
                                            .files_processed = 0,
                                            .total_files = std::nullopt,
                                            .current_entry = full_path,
                                            .is_regular_file = is_regular_file })) {
      throw std::runtime_error("extract: aborted by progress callback");
    }

    if (int const write_header_result{ archive_write_header(writer.handle, entry) };
        write_header_result != ARCHIVE_OK && write_header_result != ARCHIVE_WARN) {
      throw std::runtime_error(std::string("Failed to write entry header: ") +
                               archive_error_string(writer.handle));
    }

    if (archive_entry_size(entry) > 0) {
      std::vector<char> buffer(1024 * 1024);

      la_ssize_t bytes_read{ 0 };
      while ((bytes_read =
                  archive_read_data(reader.handle, buffer.data(), buffer.size())) > 0) {
        if (la_ssize_t const bytes_written{
                archive_write_data(writer.handle,
                                   buffer.data(),
                                   static_cast<size_t>(bytes_read)) };
            bytes_written < 0) {
          throw std::runtime_error(std::string("Failed to write entry data: ") +
                                   archive_error_string(writer.handle));
        }

        processed += static_cast<std::uint64_t>(bytes_read);

        if (options.progress &&
            !options.progress(extract_progress{ .bytes_processed = processed,
                                                .total_bytes = std::nullopt,
                                                .files_processed = 0,
                                                .total_files = std::nullopt,
                                                .current_entry = full_path,
                                                .is_regular_file = is_regular_file })) {
          throw std::runtime_error("extract: aborted by progress callback");
        }
      }

      if (bytes_read < 0) {
        throw std::runtime_error(std::string("Failed to read entry data: ") +
                                 archive_error_string(reader.handle));
      }
    }

    if (archive_write_finish_entry(writer.handle) != ARCHIVE_OK) {
      throw std::runtime_error(std::string("Failed to finish entry: ") +
                               archive_error_string(writer.handle));
    }

    if (is_regular_file) { ++files_extracted; }
  }

  if (files_extracted == 0) {
    std::string msg{ "Archive extraction failed: 0 files extracted from " +
                     archive_path.filename().string() };
    if (options.strip_components > 0) {
      msg += " with strip=" + std::to_string(options.strip_components) +
             ". Check if strip value matches archive structure";
    }
    msg += " (archive may be empty, corrupt, or unsupported format)";
    throw std::runtime_error(msg);
  }

  return files_extracted;
}

bool extract_is_archive_extension(std::filesystem::path const &path) {
  static std::unordered_set<std::string> const archive_extensions{
    ".tar",     ".tgz", ".tar.gz", ".tar.xz", ".tar.bz2",
    ".tar.zst", ".zip", ".7z",     ".rar",    ".iso"
  };

  std::string const ext{ path.extension().string() };
  if (archive_extensions.contains(ext)) { return true; }

  return path.stem().has_extension() &&
         archive_extensions.contains(path.stem().extension().string() + ext);
}

extract_totals compute_extract_totals(std::filesystem::path const &fetch_dir) {
  extract_totals totals{};
  if (!std::filesystem::exists(fetch_dir)) { return totals; }

  for (auto const &entry : std::filesystem::directory_iterator(fetch_dir)) {
    if (!entry.is_regular_file()) { continue; }
    if (entry.path().filename() == "envy-complete") { continue; }

    if (!extract_is_archive_extension(entry.path())) {
      std::error_code ec;
      totals.bytes += std::filesystem::file_size(entry.path(), ec);
      if (ec) {
        throw std::runtime_error("compute_extract_totals: failed to stat " +
                                 entry.path().string() + ": " + ec.message());
      }
      ++totals.files;
      continue;
    }

    archive_reader reader;
    if (archive_read_open_filename(reader.handle, entry.path().string().c_str(), 10240) !=
        ARCHIVE_OK) {
      throw std::runtime_error(std::string("compute_extract_totals: failed to open ") +
                               entry.path().string() + ": " +
                               archive_error_string(reader.handle));
    }

    archive_entry *ent{ nullptr };
    while (true) {
      int const r{ archive_read_next_header(reader.handle, &ent) };
      if (r == ARCHIVE_EOF) { break; }
      if (r != ARCHIVE_OK) {
        throw std::runtime_error(std::string("compute_extract_totals: header error in ") +
                                 entry.path().string() + ": " +
                                 archive_error_string(reader.handle));
      }

      if (archive_entry_filetype(ent) != AE_IFREG) { continue; }

      la_int64_t const size{ archive_entry_size(ent) };
      if (size > 0) { totals.bytes += static_cast<std::uint64_t>(size); }
      ++totals.files;
    }
  }

  return totals;
}

void extract_all_archives(std::filesystem::path const &fetch_dir,
                          std::filesystem::path const &dest_dir,
                          int strip_components,
                          std::string const &pkg_identity,
                          tui::section_handle section) {
  if (!std::filesystem::exists(fetch_dir)) {
    tui::debug("extract_all_archives: fetch_dir does not exist, nothing to extract");
    return;
  }

  // Collect items to extract
  std::vector<std::string> const items{ collect_extract_items(fetch_dir) };
  if (items.empty()) {
    tui::debug("extract_all_archives: no files to extract");
    return;
  }

  // Compute totals (with spinner if TUI enabled)
  std::optional<extract_tui_state> tui_state;
  if (section != tui::kInvalidSection) {
    std::string const label{ "[" + pkg_identity + "]" };
    tui::section_set_content(
        section,
        tui::section_frame{ .label = label,
                            .content = tui::spinner_data{
                                .text = "analyzing archive...",
                                .start_time = std::chrono::steady_clock::now() } });
  }

  extract_totals const totals{ compute_extract_totals(fetch_dir) };

  // Set up TUI state for extraction progress
  if (section != tui::kInvalidSection) {
    tui_state.emplace(section, pkg_identity, items, totals);
    tui_state->update_progress();
  }

  std::uint64_t total_files_extracted{ 0 };
  std::uint64_t total_files_copied{ 0 };
  std::uint64_t processed_bytes{ 0 };

  for (auto const &entry : std::filesystem::directory_iterator(fetch_dir)) {
    if (!entry.is_regular_file()) { continue; }

    auto const &path{ entry.path() };
    std::string const filename{ path.filename().string() };

    if (filename == "envy-complete") { continue; }

    if (tui_state && items.size() > 1) { tui_state->on_file_start(filename); }

    if (extract_is_archive_extension(path)) {
      auto const start{ std::chrono::steady_clock::now() };

      ENVY_TRACE_EXTRACT_ARCHIVE_START(pkg_identity,
                                       path.string(),
                                       dest_dir.string(),
                                       strip_components);

      auto const archive_base{ processed_bytes };
      std::uint64_t last_archive_bytes{ 0 };

      extract_options opts{ .strip_components = strip_components,
                            .progress = [&](extract_progress const &p) -> bool {
                              last_archive_bytes = p.bytes_processed;
                              if (tui_state) {
                                return tui_state->on_progress(
                                    archive_base + p.bytes_processed,
                                    p.current_entry,
                                    p.is_regular_file);
                              }
                              return true;
                            } };

      std::uint64_t const files{ extract(path, dest_dir, opts) };
      total_files_extracted += files;
      processed_bytes = archive_base + last_archive_bytes;

      auto const duration{ std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - start)
                               .count() };

      ENVY_TRACE_EXTRACT_ARCHIVE_COMPLETE(pkg_identity,
                                          path.string(),
                                          static_cast<std::int64_t>(files),
                                          duration);

      if (tui_state) { tui_state->on_progress(processed_bytes, {}, false); }
    } else {
      std::filesystem::path const dest_path{ dest_dir / filename };
      std::filesystem::copy_file(path,
                                 dest_path,
                                 std::filesystem::copy_options::overwrite_existing);

      std::error_code ec;
      processed_bytes += std::filesystem::file_size(path, ec);
      if (ec) {
        throw std::runtime_error("extract_all_archives: failed to stat " + path.string() +
                                 ": " + ec.message());
      }

      ++total_files_copied;
      if (tui_state) { tui_state->on_progress(processed_bytes, dest_path, true); }
    }
  }

  tui::debug(
      "extract_all_archives: complete (%llu files from archives, %llu files copied)",
      static_cast<unsigned long long>(total_files_extracted),
      static_cast<unsigned long long>(total_files_copied));
}

}  // namespace envy
