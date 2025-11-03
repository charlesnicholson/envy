#include "fetch.h"

#include "aws_util.h"
#include "libcurl_util.h"

#include <filesystem>
#include <stdexcept>
#include <string>
#include <system_error>

namespace envy {
namespace {

std::filesystem::path prepare_destination(std::filesystem::path destination) {
  if (destination.empty()) {
    throw std::invalid_argument("fetch: destination path is empty");
  }

  if (!destination.is_absolute()) { destination = std::filesystem::absolute(destination); }
  destination = destination.lexically_normal();

  if (auto const parent{ destination.parent_path() }; !parent.empty()) {
    std::error_code ec;
    std::filesystem::create_directories(parent, ec);
    if (ec) {
      throw std::runtime_error("fetch: failed to create destination parent: " +
                               parent.string() + ": " + ec.message());
    }
  }

  return destination;
}

std::filesystem::path resolve_file_path(
    std::string const &canonical_path,
    std::optional<std::filesystem::path> const &file_root) {
  std::filesystem::path source{ canonical_path };
  return std::filesystem::absolute(source.is_relative() && file_root ? *file_root / source
                                                                     : source)
      .lexically_normal();
}

fetch_result fetch_local_file(std::string const &canonical_path,
                              std::filesystem::path const &destination,
                              std::optional<std::filesystem::path> const &file_root) {
  auto const source{ resolve_file_path(canonical_path, file_root) };

  // Validate source exists
  std::error_code ec;
  if (!std::filesystem::exists(source, ec)) {
    throw std::runtime_error("fetch: source file does not exist: " + source.string());
  }
  if (ec) {
    throw std::runtime_error("fetch: failed to check source: " + source.string() + ": " +
                             ec.message());
  }

  auto const dest{ prepare_destination(destination) };

  bool const is_directory{ std::filesystem::is_directory(source, ec) };
  if (ec) {
    throw std::runtime_error("fetch: failed to check if source is directory: " +
                             source.string() + ": " + ec.message());
  }

  if (is_directory) {
    std::filesystem::copy(source,
                          dest,
                          std::filesystem::copy_options::recursive |
                              std::filesystem::copy_options::overwrite_existing,
                          ec);
    if (ec) {
      throw std::runtime_error("fetch: failed to copy directory: " + source.string() +
                               " -> " + dest.string() + ": " + ec.message());
    }
  } else {
    std::filesystem::copy_file(source,
                               dest,
                               std::filesystem::copy_options::overwrite_existing,
                               ec);
    if (ec) {
      throw std::runtime_error("fetch: failed to copy file: " + source.string() + " -> " +
                               dest.string() + ": " + ec.message());
    }
  }

  return fetch_result{ .scheme = uri_scheme::LOCAL_FILE_ABSOLUTE,
                       .resolved_source = source,
                       .resolved_destination = dest };
}

}  // namespace

fetch_result fetch(fetch_request const &request) {
  auto const info{ uri_classify(request.source) };
  if (info.canonical.empty() && info.scheme == uri_scheme::UNKNOWN) {
    throw std::invalid_argument("fetch: source URI is empty");
  }

  switch (info.scheme) {
    case uri_scheme::LOCAL_FILE_ABSOLUTE:
    case uri_scheme::LOCAL_FILE_RELATIVE: {
      return fetch_local_file(info.canonical, request.destination, request.file_root);
    }

    case uri_scheme::HTTP:
    case uri_scheme::HTTPS: {
      return fetch_result{ .scheme = info.scheme,
                           .resolved_source = std::filesystem::path{ info.canonical },
                           .resolved_destination = libcurl_download(info.canonical,
                                                                    request.destination,
                                                                    request.progress) };
    }
    case uri_scheme::S3: {
      auto const resolved_destination{ prepare_destination(request.destination) };

      aws_s3_download(s3_download_request{ .uri = info.canonical,
                                           .destination = resolved_destination,
                                           .region = request.region,
                                           .progress = request.progress });
      return fetch_result{
        .scheme = info.scheme,
        .resolved_source = std::filesystem::path{ info.canonical },
        .resolved_destination = resolved_destination,
      };
    }

    default: throw std::runtime_error("fetch: scheme not implemented");
  }
}

}  // namespace envy
