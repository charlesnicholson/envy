#include "cmd_extract.h"
#include "tui.h"

#include "archive.h"
#include "archive_entry.h"

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace envy {
namespace {

struct archive_reader {
  archive *a{ archive_read_new() };

  archive_reader() {
    if (!a) { throw std::runtime_error("libarchive read allocation failed"); }
    archive_read_support_filter_all(a);
    archive_read_support_format_all(a);
  }

  ~archive_reader() {
    if (a) {
      archive_read_close(a);
      archive_read_free(a);
    }
  }

  archive_reader(archive_reader const &) = delete;
  archive_reader &operator=(archive_reader const &) = delete;
};

struct archive_writer {
  archive *a{ archive_write_disk_new() };

  archive_writer() {
    if (!a) { throw std::runtime_error("libarchive write allocation failed"); }
    archive_write_disk_set_options(a,
                                   ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_PERM |
                                       ARCHIVE_EXTRACT_ACL | ARCHIVE_EXTRACT_FFLAGS);
    archive_write_disk_set_standard_lookup(a);
  }

  ~archive_writer() {
    if (a) {
      archive_write_close(a);
      archive_write_free(a);
    }
  }

  archive_writer(archive_writer const &) = delete;
  archive_writer &operator=(archive_writer const &) = delete;
};

void extract_archive(std::filesystem::path const &archive_path,
                     std::filesystem::path const &destination) {
  archive_reader reader;
  archive_writer writer;

  if (archive_read_open_filename(reader.a, archive_path.string().c_str(), 10240) !=
      ARCHIVE_OK) {
    throw std::runtime_error(std::string("Failed to open archive: ") +
                             archive_error_string(reader.a));
  }

  archive_entry *entry{ nullptr };

  while (true) {
    int const r{ archive_read_next_header(reader.a, &entry) };
    if (r == ARCHIVE_EOF) { break; }

    if (r != ARCHIVE_OK) {
      throw std::runtime_error(std::string("Failed to read archive header: ") +
                               archive_error_string(reader.a));
    }

    char const *entry_path{ archive_entry_pathname(entry) };
    if (!entry_path) { throw std::runtime_error("Archive entry has null pathname"); }

    std::filesystem::path const full_path{ destination / entry_path };
    auto const parent{ full_path.parent_path() };
    if (!parent.empty()) {
      std::error_code ec;
      std::filesystem::create_directories(parent, ec);
      if (ec) {
        throw std::runtime_error("Failed to create directory " + parent.string() + ": " +
                                 ec.message());
      }
    }

    std::string const full_path_str{ full_path.string() };
    archive_entry_copy_pathname(entry, full_path_str.c_str());

    if (char const *hardlink{ archive_entry_hardlink(entry) }) {
      auto const hardlink_full{ (destination / hardlink).string() };
      archive_entry_copy_hardlink(entry, hardlink_full.c_str());
    }

    int write_header_result{ archive_write_header(writer.a, entry) };
    if (write_header_result != ARCHIVE_OK && write_header_result != ARCHIVE_WARN) {
      throw std::runtime_error(std::string("Failed to write entry header: ") +
                               archive_error_string(writer.a));
    }

    if (archive_entry_size(entry) > 0) {
      char buffer[8192];
      la_ssize_t bytes_read{ 0 };
      while ((bytes_read = archive_read_data(reader.a, buffer, sizeof(buffer))) > 0) {
        if (la_ssize_t const bytes_written{
                archive_write_data(writer.a, buffer, static_cast<size_t>(bytes_read)) };
            bytes_written < 0) {
          throw std::runtime_error(std::string("Failed to write entry data: ") +
                                   archive_error_string(writer.a));
        }
      }

      if (bytes_read < 0) {
        throw std::runtime_error(std::string("Failed to read entry data: ") +
                                 archive_error_string(reader.a));
      }
    }

    if (archive_write_finish_entry(writer.a) != ARCHIVE_OK) {
      throw std::runtime_error(std::string("Failed to finish entry: ") +
                               archive_error_string(writer.a));
    }
  }
}

}  // anonymous namespace

cmd_extract::cmd_extract(cmd_extract::cfg cfg) : cfg_{ std::move(cfg) } {}

void cmd_extract::schedule(tbb::flow::graph &g) {
  node_.emplace(g, [this](tbb::flow::continue_msg const &) {
    std::filesystem::path destination{ cfg_.destination };
    if (destination.empty()) { destination = std::filesystem::current_path(); }

    std::error_code ec;
    if (!std::filesystem::exists(cfg_.archive_path, ec)) {
      tui::error("Failed to extract: archive not found: %s",
                 cfg_.archive_path.string().c_str());
      succeeded_ = false;
      return;
    }

    if (!std::filesystem::is_regular_file(cfg_.archive_path, ec)) {
      tui::error("Failed to extract: not a regular file: %s",
                 cfg_.archive_path.string().c_str());
      succeeded_ = false;
      return;
    }

    if (!std::filesystem::exists(destination, ec)) {
      std::filesystem::create_directories(destination, ec);
      if (ec) {
        tui::error("Failed to create destination directory: %s", ec.message().c_str());
        succeeded_ = false;
        return;
      }
    }

    if (!std::filesystem::is_directory(destination, ec)) {
      tui::error("Destination is not a directory: %s", destination.string().c_str());
      succeeded_ = false;
      return;
    }

    try {
      tui::info("Extracting %s to %s",
                cfg_.archive_path.filename().string().c_str(),
                destination.string().c_str());

      extract_archive(cfg_.archive_path, destination);

      // Determine root of extracted tree (tar archives may have leading directory)
      std::filesystem::path base{ destination / "root" };
      if (!std::filesystem::exists(base)) { base = destination; }

      std::uint64_t file_count{ 0 };
      for (auto const &entry : std::filesystem::recursive_directory_iterator(base)) {
        if (!entry.is_regular_file()) { continue; }
        if (entry.path().filename().string().rfind("._", 0) == 0) { continue; }
        ++file_count;
      }

      tui::info("Extracted %llu files", static_cast<unsigned long long>(file_count));
      succeeded_ = true;
    } catch (std::exception const &ex) {
      tui::error("Extraction failed: %s", ex.what());
      succeeded_ = false;
    }
  });

  node_->try_put(tbb::flow::continue_msg{});
}

}  // namespace envy
