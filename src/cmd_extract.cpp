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

std::uint64_t extract_archive(std::filesystem::path const &archive_path,
                              std::filesystem::path const &destination) {
  archive *reader{ archive_read_new() };
  if (!reader) { throw std::runtime_error("libarchive read allocation failed"); }

  archive_read_support_filter_all(reader);
  archive_read_support_format_all(reader);

  archive *writer{ archive_write_disk_new() };
  if (!writer) {
    archive_read_free(reader);
    throw std::runtime_error("libarchive write allocation failed");
  }

  archive_write_disk_set_options(writer,
                                 ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_PERM |
                                     ARCHIVE_EXTRACT_ACL | ARCHIVE_EXTRACT_FFLAGS);
  archive_write_disk_set_standard_lookup(writer);

  if (archive_read_open_filename(reader, archive_path.string().c_str(), 10240) !=
      ARCHIVE_OK) {
    std::string const message{ std::string("Failed to open archive: ") +
                               archive_error_string(reader) };
    archive_write_free(writer);
    archive_read_free(reader);
    throw std::runtime_error(message);
  }

  archive_entry *entry{ nullptr };
  std::uint64_t file_count{ 0 };

  while (true) {
    int const r{ archive_read_next_header(reader, &entry) };
    if (r == ARCHIVE_EOF) { break; }

    if (r != ARCHIVE_OK) {
      std::string const message{ std::string("Failed to read archive header: ") +
                                 archive_error_string(reader) };
      archive_read_close(reader);
      archive_write_close(writer);
      archive_read_free(reader);
      archive_write_free(writer);
      throw std::runtime_error(message);
    }

    char const *entry_path{ archive_entry_pathname(entry) };
    if (!entry_path) {
      archive_read_close(reader);
      archive_write_close(writer);
      archive_read_free(reader);
      archive_write_free(writer);
      throw std::runtime_error("Archive entry has null pathname");
    }

    std::filesystem::path const full_path{ destination / entry_path };
    auto const parent{ full_path.parent_path() };
    if (!parent.empty()) {
      std::error_code ec;
      std::filesystem::create_directories(parent, ec);
      if (ec) {
        archive_read_close(reader);
        archive_write_close(writer);
        archive_read_free(reader);
        archive_write_free(writer);
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

    int write_header_result{ archive_write_header(writer, entry) };
    if (write_header_result != ARCHIVE_OK && write_header_result != ARCHIVE_WARN) {
      std::string const message{ std::string("Failed to write entry header: ") +
                                 archive_error_string(writer) };
      archive_read_close(reader);
      archive_write_close(writer);
      archive_read_free(reader);
      archive_write_free(writer);
      throw std::runtime_error(message);
    }

    if (archive_entry_size(entry) > 0) {
      char buffer[8192];
      ssize_t bytes_read{ 0 };
      while ((bytes_read = archive_read_data(reader, buffer, sizeof(buffer))) > 0) {
        ssize_t bytes_written{ archive_write_data(writer, buffer, bytes_read) };
        if (bytes_written < 0) {
          std::string const message{ std::string("Failed to write entry data: ") +
                                     archive_error_string(writer) };
          archive_read_close(reader);
          archive_write_close(writer);
          archive_read_free(reader);
          archive_write_free(writer);
          throw std::runtime_error(message);
        }
      }

      if (bytes_read < 0) {
        std::string const message{ std::string("Failed to read entry data: ") +
                                   archive_error_string(reader) };
        archive_read_close(reader);
        archive_write_close(writer);
        archive_read_free(reader);
        archive_write_free(writer);
        throw std::runtime_error(message);
      }
    }

    if (archive_write_finish_entry(writer) != ARCHIVE_OK) {
      std::string const message{ std::string("Failed to finish entry: ") +
                                 archive_error_string(writer) };
      archive_read_close(reader);
      archive_write_close(writer);
      archive_read_free(reader);
      archive_write_free(writer);
      throw std::runtime_error(message);
    }

    mode_t const filetype{ archive_entry_filetype(entry) };
    if (filetype == AE_IFREG) { ++file_count; }
  }

  archive_read_close(reader);
  archive_write_close(writer);
  archive_read_free(reader);
  archive_write_free(writer);

  return file_count;
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

      std::uint64_t const file_count{ extract_archive(cfg_.archive_path, destination) };

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
