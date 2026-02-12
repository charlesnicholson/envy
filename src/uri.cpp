#include "uri.h"

#include "util.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>

namespace envy {
namespace {

constexpr auto to_lower = [](unsigned char c) { return std::tolower(c); };

std::string_view trim(std::string_view value) {
  auto const first{ value.find_first_not_of(" \t\n\r\f\v") };
  if (first == std::string_view::npos) { return {}; }

  auto const last{ value.find_last_not_of(" \t\n\r\f\v") };
  return value.substr(first, last - first + 1);
}

bool istarts_with(std::string_view value, std::string_view prefix) {
  if (prefix.size() > value.size()) { return false; }
  return std::ranges::equal(prefix,
                            value | std::views::take(prefix.size()),
                            {},
                            to_lower,
                            to_lower);
}

bool iends_with(std::string_view value, std::string_view suffix) {
  if (suffix.size() > value.size()) { return false; }
  return std::ranges::equal(suffix,
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

  if ((path[0] == '/' || path[0] == '\\') &&
      std::isalpha(static_cast<unsigned char>(path[1])) && path[2] == ':') {
    return true;
  }

  return false;
}

std::string strip_file_scheme(std::string_view uri) {
  std::string cand{ uri.substr(7) };

  if (!cand.empty() && cand[0] == '/' && cand.size() >= 3 &&
      std::isalpha(static_cast<unsigned char>(cand[1])) && cand[2] == ':') {
    cand.erase(cand.begin());
    return cand;
  }

  if (is_drive_letter_path(cand)) { return cand; }

  if (!cand.empty() && cand[0] == '/' && cand.size() > 1 && cand[1] == '/') {
    return cand;
  }

  auto const slash{ cand.find('/') };
  if (slash == std::string::npos) { return cand; }

  std::string_view const host{ std::string_view{ cand }.substr(0, slash) };
  std::string_view const tail{ std::string_view{ cand }.substr(slash) };

  if (host.empty() || iequals(host, "localhost")) { return std::string{ tail }; }
  if (host.find(':') != std::string_view::npos) { return cand; }

  return std::string{ "//" }.append(host).append(tail);
}

std::filesystem::path base_directory(std::optional<std::filesystem::path> const &root) {
  if (root && !root->empty()) { return std::filesystem::absolute(*root); }
  return std::filesystem::current_path();
}

}  // namespace

uri_info uri_classify(std::string_view value) {
  auto canonical{ std::string{ trim(value) } };
  if (canonical.empty()) { return uri_info{ uri_scheme::UNKNOWN, std::move(canonical) }; }

  auto const path_segment{ strip_query_and_fragment(canonical) };

  // Git protocol schemes (no SSL certs needed)
  if (istarts_with(canonical, "git://") || istarts_with(canonical, "git+ssh://")) {
    return uri_info{ uri_scheme::GIT, std::move(canonical) };
  }

  // HTTPS git repos (SSL certs needed)
  if (istarts_with(canonical, "https://") && iends_with(path_segment, ".git")) {
    return uri_info{ uri_scheme::GIT_HTTPS, std::move(canonical) };
  }

  // Non-git .git suffix with other schemes - treat as git protocol
  if (iends_with(path_segment, ".git")) {
    return uri_info{ uri_scheme::GIT, std::move(canonical) };
  }

  if (istarts_with(canonical, "s3://")) {
    return uri_info{ uri_scheme::S3, std::move(canonical) };
  }
  if (istarts_with(canonical, "https://")) {
    return uri_info{ uri_scheme::HTTPS, std::move(canonical) };
  }
  if (istarts_with(canonical, "http://")) {
    return uri_info{ uri_scheme::HTTP, std::move(canonical) };
  }
  if (istarts_with(canonical, "ftps://")) {
    return uri_info{ uri_scheme::FTPS, std::move(canonical) };
  }
  if (istarts_with(canonical, "ftp://")) {
    return uri_info{ uri_scheme::FTP, std::move(canonical) };
  }
  if (istarts_with(canonical, "scp://") || istarts_with(canonical, "ssh://")) {
    return uri_info{ uri_scheme::SSH, std::move(canonical) };
  }
  if (looks_like_scp_uri(canonical)) {
    return uri_info{ uri_scheme::SSH, std::move(canonical) };
  }

  std::string local_source{};

  if (istarts_with(canonical, "file://")) {
    local_source = strip_file_scheme(canonical);
  } else {
    if (canonical.find("://") != std::string_view::npos) {
      return uri_info{ uri_scheme::UNKNOWN, std::move(canonical) };
    }
    local_source = canonical;
  }

  uri_scheme scheme{};
#ifdef _WIN32
  std::filesystem::path local_path{ local_source };

  // On Windows, paths with leading slash are absolute (mapped to current drive)
  bool has_leading_slash{ !local_source.empty() &&
                          (local_source[0] == '/' || local_source[0] == '\\') };
  scheme = (local_path.is_absolute() || has_leading_slash)
               ? uri_scheme::LOCAL_FILE_ABSOLUTE
               : uri_scheme::LOCAL_FILE_RELATIVE;

  // Only convert POSIX-style absolute paths (/path) to backslashes
  // Preserve original separators for drive letters (C:/) and UNC (\\server or //server)
  if (scheme == uri_scheme::LOCAL_FILE_ABSOLUTE && !local_path.has_root_name() &&
      has_leading_slash) {
    // This is a POSIX-style path like "/tmp" - convert to backslashes for Windows
    std::ranges::replace(local_source, '/', '\\');
  }
  // Otherwise preserve the original string as-is
#else
  scheme = std::filesystem::path{ local_source }.is_absolute()
               ? uri_scheme::LOCAL_FILE_ABSOLUTE
               : uri_scheme::LOCAL_FILE_RELATIVE;
#endif
  return uri_info{ scheme, std::move(local_source) };
}

std::filesystem::path uri_resolve_local_file_relative(
    std::string_view local_file,
    std::optional<std::filesystem::path> const &anchor) {
  auto const trimmed{ trim(local_file) };
  if (trimmed.empty()) { throw std::invalid_argument("resolve_local_uri: empty value"); }

  auto const info{ uri_classify(trimmed) };
  auto const scheme{ info.scheme };

  if (scheme != uri_scheme::LOCAL_FILE_ABSOLUTE &&
      scheme != uri_scheme::LOCAL_FILE_RELATIVE) {
    throw std::invalid_argument("resolve_local_uri: value is not a local file");
  }

  if (info.canonical.empty()) {
    throw std::invalid_argument("resolve_local_uri: resolved path is empty");
  }

  std::filesystem::path resolved{ info.canonical };
  if (scheme == uri_scheme::LOCAL_FILE_RELATIVE) {
    resolved = std::filesystem::absolute(base_directory(anchor) / resolved);
  }
#ifdef _WIN32
  else if (scheme == uri_scheme::LOCAL_FILE_ABSOLUTE) {
    // Path with root directory but no drive (e.g., "\tmp") - add current drive
    if (!resolved.has_root_name() && resolved.has_root_directory()) {
      auto drive{ std::filesystem::current_path().root_name().string() };
      // Concatenate drive with the path that includes root directory
      resolved = std::filesystem::path(drive + info.canonical);
    }
  }
#endif

  return resolved.lexically_normal();
}

bool uri_is_http_scheme(std::string_view url) {
  return istarts_with(url, "http://") || istarts_with(url, "https://");
}

bool uri_is_https_scheme(std::string_view url) {
  return istarts_with(url, "https://");
}

std::string uri_extract_filename(std::string_view uri) {
  if (uri.empty()) { return {}; }

  // Strip query and fragment first
  auto const clean_uri{ strip_query_and_fragment(uri) };
  if (clean_uri.empty()) { return {}; }

  // Find last slash or backslash (Windows paths)
  auto const last_fwd_slash{ clean_uri.rfind('/') };
  auto const last_back_slash{ clean_uri.rfind('\\') };

  // Choose the rightmost slash, handling npos correctly
  size_t last_slash{};
  if (last_fwd_slash == std::string_view::npos &&
      last_back_slash == std::string_view::npos) {  // No slash: filename or domain/scheme
    return std::string{ clean_uri };
  } else if (last_fwd_slash == std::string_view::npos) {
    last_slash = last_back_slash;
  } else if (last_back_slash == std::string_view::npos) {
    last_slash = last_fwd_slash;
  } else {
    last_slash = std::max(last_fwd_slash, last_back_slash);
  }

  // Extract everything after last slash
  auto const filename{ clean_uri.substr(last_slash + 1) };
  if (filename.empty()) { return {}; }  // URI ends with slash

  // URL decode: convert %XX sequences to actual characters
  std::string decoded;
  decoded.reserve(filename.size());

  for (size_t i = 0; i < filename.size(); ++i) {
    if (filename[i] == '%' && i + 2 < filename.size()) {
      int const high{ util_hex_char_to_int(filename[i + 1]) };
      int const low{ util_hex_char_to_int(filename[i + 2]) };

      if (high >= 0 && low >= 0) {
        decoded.push_back(static_cast<char>((high << 4) | low));
        i += 2;
        continue;
      }
    }
    decoded.push_back(filename[i]);
  }

  return decoded;
}

}  // namespace envy
