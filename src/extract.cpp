#include "extract.h"
#include "trace.h"
#include "tui.h"
#include "util.h"

#include "archive.h"
#include "archive_entry.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
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

std::optional<std::string> strip_path_components(char const *path, int strip_count) {
  if (!path) { return std::nullopt; }
  if (strip_count <= 0) { return std::string(path); }

  char const *p{ path };
  int components_stripped{ 0 };

  // Skip leading slashes
  while (*p == '/') { ++p; }

  // Skip the specified number of path components
  while (components_stripped < strip_count) {
    if (*p == '\0') {
      // Path has fewer components than requested strip count
      return std::nullopt;
    }
    if (*p == '/') {
      ++components_stripped;
      // Skip multiple consecutive slashes
      while (*p == '/') { ++p; }
    } else {
      ++p;
    }
  }

  if (*p == '\0') {
    // Nothing left after stripping
    return std::nullopt;
  }

  return std::string(p);
}

}  // namespace

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

    // Apply strip-components if configured
    std::string stripped_path;
    if (options.strip_components > 0) {
      auto stripped{ strip_path_components(entry_path, options.strip_components) };
      if (!stripped) {
        // Skip this entry - it was stripped to nothing
        continue;
      }
      stripped_path = *stripped;
      entry_path = stripped_path.c_str();
    }

    std::filesystem::path const full_path{ destination / entry_path };
    ensure_directory(full_path);

    {
      std::string const full_path_str{ full_path.string() };
      archive_entry_copy_pathname(entry, full_path_str.c_str());
    }

    if (char const *hardlink{ archive_entry_hardlink(entry) }) {
      std::string hardlink_str{ hardlink };

      // Strip components from hardlink target too
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
                          extract_progress_cb_t progress,
                          std::string const &pkg_identity,
                          std::function<void(std::string const &)> on_file,
                          std::optional<extract_totals> totals_hint) {
  if (!std::filesystem::exists(fetch_dir)) {
    tui::debug("extract_all_archives: fetch_dir does not exist, nothing to extract");
    return;
  }

  extract_totals const totals{ totals_hint.value_or(compute_extract_totals(fetch_dir)) };

  std::uint64_t files_processed{ 0 };
  std::uint64_t processed_bytes{ 0 };
  std::filesystem::path last_file_seen;

  auto emit_progress = [&](std::uint64_t bytes,
                           std::filesystem::path const &entry,
                           bool is_regular_file) -> bool {
    if (!progress) { return true; }

    if (is_regular_file && entry != last_file_seen) {
      ++files_processed;
      last_file_seen = entry;
    }

    return progress(extract_progress{
        .bytes_processed = bytes,
        .total_bytes = totals.bytes > 0 ? std::make_optional(totals.bytes) : std::nullopt,
        .files_processed = files_processed,
        .total_files = totals.files > 0 ? std::make_optional(totals.files) : std::nullopt,
        .current_entry = entry,
        .is_regular_file = is_regular_file });
  };

  std::uint64_t total_files_extracted{ 0 };
  std::uint64_t total_files_copied{ 0 };

  for (auto const &entry : std::filesystem::directory_iterator(fetch_dir)) {
    if (!entry.is_regular_file()) { continue; }

    auto const &path{ entry.path() };
    std::string const filename{ path.filename().string() };

    if (filename == "envy-complete") { continue; }

    if (on_file) { on_file(filename); }

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
                              return emit_progress(archive_base + p.bytes_processed,
                                                   p.current_entry,
                                                   p.is_regular_file);
                            } };

      std::uint64_t const files{ extract(path, dest_dir, opts) };
      // last_archive_bytes is already updated via the progress callback
      total_files_extracted += files;
      processed_bytes = archive_base + last_archive_bytes;

      auto const duration{ std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - start)
                               .count() };

      ENVY_TRACE_EXTRACT_ARCHIVE_COMPLETE(pkg_identity,
                                          path.string(),
                                          static_cast<std::int64_t>(files),
                                          duration);

      // Refresh progress with updated totals (no new file counted here)
      emit_progress(processed_bytes, {}, false);
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
      emit_progress(processed_bytes, dest_path, true);
    }
  }

  tui::debug(
      "extract_all_archives: complete (%llu files from archives, %llu files copied)",
      static_cast<unsigned long long>(total_files_extracted),
      static_cast<unsigned long long>(total_files_copied));
}

}  // namespace envy
