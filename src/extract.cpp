#include "extract.h"
#include "util.h"

#include "archive.h"
#include "archive_entry.h"

#include <cstdint>
#include <filesystem>
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
  if (strip_count <= 0 || !path) { return std::string(path); }

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
                                            .current_entry = full_path })) {
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
                                                .current_entry = full_path })) {
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

    if (archive_entry_filetype(entry) == AE_IFREG) { ++files_extracted; }
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

}  // namespace envy
