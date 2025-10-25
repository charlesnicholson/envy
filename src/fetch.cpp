#include "fetch.h"

#include "libcurl_util.h"
#include "aws_util.h"

#include <algorithm>
#include <cctype>
#include <ranges>
#include <stdexcept>
#include <string>

namespace envy {
namespace detail {

constexpr auto to_lower{ [](unsigned char c) { return std::tolower(c); } };

std::string_view trim(std::string_view value) {
  auto const first{ value.find_first_not_of(" \t\n\r\f\v") };
  if (first == std::string_view::npos) { return {}; }
  return value.substr(first, value.find_last_not_of(" \t\n\r\f\v") - first + 1);
}

bool istarts_with(std::string_view value, std::string_view prefix) {
  return prefix.size() > value.size()
             ? false
             : std::ranges::equal(prefix,
                                  value | std::views::take(prefix.size()),
                                  {},
                                  to_lower,
                                  to_lower);
}

bool iends_with(std::string_view value, std::string_view suffix) {
  return suffix.size() > value.size()
             ? false
             : std::ranges::equal(suffix,
                                  value | std::views::drop(value.size() - suffix.size()),
                                  {},
                                  to_lower,
                                  to_lower);
}

bool iequals(std::string_view lhs, std::string_view rhs) {
  return std::ranges::equal(lhs, rhs, {}, to_lower, to_lower);
}

std::string_view strip_query_and_fragment(std::string_view uri) {
  auto const pos{ uri.find_first_of("?#") };
  return pos == std::string_view::npos ? uri : uri.substr(0, pos);
}

bool looks_like_scp_uri(std::string_view uri) {
  if (uri.find("://") != std::string_view::npos) { return false; }

  auto const colon{ uri.find(':') };
  if (colon == std::string_view::npos || colon + 1 >= uri.size()) { return false; }

  auto const user_host{ uri.substr(0, colon) };
  auto const at{ user_host.find('@') };

  return at != std::string_view::npos && at > 0;
}

bool is_drive_letter_path(std::string_view path) {
  if (path.size() < 2) { return false; }
  if (std::isalpha(static_cast<unsigned char>(path[0])) && path[1] == ':') { return true; }
  if (path.size() < 3) { return false; }

  return (path[0] == '/' || path[0] == '\\') &&
         std::isalpha(static_cast<unsigned char>(path[1])) && path[2] == ':';
}

std::string strip_file_scheme(std::string_view uri) {
  std::string candidate{ uri.substr(7) };

  if (!candidate.empty() && candidate[0] == '/' && candidate.size() >= 3 &&
      std::isalpha(static_cast<unsigned char>(candidate[1])) && candidate[2] == ':') {
    candidate.erase(candidate.begin());
    return candidate;
  }

  if (is_drive_letter_path(candidate)) { return candidate; }

  if (!candidate.empty() && candidate[0] == '/' && candidate.size() > 1 &&
      candidate[1] == '/') {
    return candidate;
  }

  auto const slash{ candidate.find('/') };
  if (slash == std::string::npos) { return candidate; }

  std::string_view const host{ std::string_view{ candidate }.substr(0, slash) };
  std::string_view const tail{ std::string_view{ candidate }.substr(slash) };

  if (host.empty() || iequals(host, "localhost")) { return std::string{ tail }; }
  if (host.find(':') != std::string_view::npos) { return candidate; }

  return std::string{ "//" }.append(host).append(tail);
}

std::filesystem::path resolve_local_source(
    std::string_view source,
    std::optional<std::filesystem::path> const &root) {
  auto const trimmed{ trim(source) };
  if (trimmed.empty()) {
    throw std::invalid_argument("resolve_local_source: empty source");
  }

  std::filesystem::path local_path{ istarts_with(trimmed, "file://")
                                        ? strip_file_scheme(trimmed)
                                        : std::string{ trimmed } };
  if (local_path.empty()) {
    throw std::invalid_argument("resolve_local_source: resolved path is empty");
  }

  auto const base_dir{ (root && !root->empty()) ? std::filesystem::absolute(*root)
                                                : std::filesystem::current_path() };

  auto const resolved{ local_path.is_absolute()
                           ? local_path
                           : std::filesystem::absolute(base_dir / local_path) };

  return resolved.lexically_normal();
}

std::filesystem::path prepare_destination(std::filesystem::path destination) {
  if (destination.empty()) {
    throw std::invalid_argument("fetch: destination path is empty");
  }

  if (!destination.is_absolute()) {
    destination = std::filesystem::absolute(destination);
  }
  destination = destination.lexically_normal();

  std::error_code ec;
  auto const parent{ destination.parent_path() };
  if (!parent.empty()) {
    std::filesystem::create_directories(parent, ec);
    if (ec) {
      throw std::runtime_error("fetch: failed to create destination parent: " +
                               parent.string() + ": " + ec.message());
    }
  }

  return destination;
}

}  // namespace detail

fetch_scheme fetch_classify(std::string_view uri) {
  auto const trimmed{ detail::trim(uri) };
  if (trimmed.empty()) { return fetch_scheme::UNKNOWN; }

  auto const path_segment{ detail::strip_query_and_fragment(trimmed) };
  if (detail::iends_with(path_segment, ".git")) { return fetch_scheme::GIT; }

  if (detail::istarts_with(trimmed, "git://") ||
      detail::istarts_with(trimmed, "git+ssh://")) {
    return fetch_scheme::GIT;
  }

  if (detail::istarts_with(trimmed, "s3://")) { return fetch_scheme::S3; }
  if (detail::istarts_with(trimmed, "https://")) { return fetch_scheme::HTTPS; }
  if (detail::istarts_with(trimmed, "http://")) { return fetch_scheme::HTTP; }
  if (detail::istarts_with(trimmed, "ftps://")) { return fetch_scheme::FTPS; }
  if (detail::istarts_with(trimmed, "ftp://")) { return fetch_scheme::FTP; }
  if (detail::istarts_with(trimmed, "scp://")) { return fetch_scheme::SSH; }
  if (detail::istarts_with(trimmed, "ssh://")) { return fetch_scheme::SSH; }
  if (detail::istarts_with(trimmed, "file://")) { return fetch_scheme::LOCAL_FILE; }

  if (detail::looks_like_scp_uri(trimmed)) { return fetch_scheme::SSH; }

  if (trimmed.find("://") != std::string_view::npos) { return fetch_scheme::UNKNOWN; }

  return fetch_scheme::LOCAL_FILE;
}

fetch_result fetch(fetch_request const &request) {
  std::string_view const trimmed{ detail::trim(request.source) };
  if (trimmed.empty()) { throw std::invalid_argument("fetch: source URI is empty"); }

  fetch_scheme const scheme{ fetch_classify(request.source) };

  switch (scheme) {
    case fetch_scheme::HTTP:
    case fetch_scheme::HTTPS: {
      return fetch_result{
        .scheme = scheme,
        .resolved_source = std::filesystem::path{ std::string{ trimmed } },
        .resolved_destination =
            libcurl_download(trimmed, request.destination, request.progress)
      };
    }
    case fetch_scheme::S3: {
      auto resolved_destination{
          detail::prepare_destination(request.destination) };
      s3_download_request s3_request{
        .uri = std::string{ trimmed },
        .destination = resolved_destination,
        .region = request.region,
        .progress = request.progress,
      };
      aws_s3_download(s3_request);
      return fetch_result{
        .scheme = scheme,
        .resolved_source = std::filesystem::path{ std::string{ trimmed } },
        .resolved_destination = std::move(resolved_destination),
      };
    }

    default: throw std::runtime_error("fetch: scheme not implemented");
  }
}

}  // namespace envy
