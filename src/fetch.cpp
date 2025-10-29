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

}  // namespace

fetch_result fetch(fetch_request const &request) {
  auto const info{ uri_classify(request.source) };
  if (info.canonical.empty() && info.scheme == uri_scheme::UNKNOWN) {
    throw std::invalid_argument("fetch: source URI is empty");
  }

  switch (info.scheme) {
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
