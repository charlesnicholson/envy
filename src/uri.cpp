#include "uri.h"

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
  if (iends_with(path_segment, ".git")) {
    return uri_info{ uri_scheme::GIT, std::move(canonical) };
  }
  if (istarts_with(canonical, "git://") || istarts_with(canonical, "git+ssh://")) {
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
  auto is_drive_letter = [](std::string const &s) {
    return s.size() >= 2 && std::isalpha(static_cast<unsigned char>(s[0])) && s[1] == ':';
  };
  auto is_unc_like = [](std::string const &s) {
    return s.size() >= 2 && s[0] == '/' && s[1] == '/';
  };
  // Treat leading single '/' (POSIX style) and leading '\\' as absolute on Windows too.
  bool absoluteish = std::filesystem::path{ local_source }.is_absolute() ||
                     (!local_source.empty() && (local_source[0] == '/' || local_source[0] == '\\'));
  scheme = absoluteish ? uri_scheme::LOCAL_FILE_ABSOLUTE : uri_scheme::LOCAL_FILE_RELATIVE;

  if (scheme == uri_scheme::LOCAL_FILE_ABSOLUTE) {
    // Convert POSIX-style absolute (/path/...) to backslashes except the solitary root "/".
    if (!local_source.empty() && local_source[0] == '/' && local_source.size() > 1 &&
        !is_unc_like(local_source)) {
      for (auto &ch : local_source) {
        if (ch == '/') ch = '\\';
      }
    }
    // Leave drive-letter paths (C:/ or C:\) as-is to preserve forward slashes per tests.
    // Leave UNC forward-slash form (//server/share) and backslash form (\\server\share) unchanged.
  }
#else
  scheme = std::filesystem::path{ local_source }.is_absolute() ? uri_scheme::LOCAL_FILE_ABSOLUTE
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

  auto const &raw_path{ info.canonical };
  if (raw_path.empty()) {
    throw std::invalid_argument("resolve_local_uri: resolved path is empty");
  }

  std::filesystem::path resolved{ raw_path };
  if (scheme == uri_scheme::LOCAL_FILE_RELATIVE) {
    auto const base{ base_directory(anchor) };
    resolved = std::filesystem::absolute(base / resolved);
  }
#ifdef _WIN32
  else {
    // Handle POSIX-style absolute canonical paths (leading '/') that were converted to backslashes
    // earlier and now look like "\\tmp\\...". Map these to the current drive root to produce an
    // absolute Windows path matching test expectations (e.g., C:\\tmp\\...).
    if (!raw_path.empty() && (raw_path[0] == '\\' || raw_path[0] == '/')) {
      bool is_unc = false;
      if (raw_path.size() >= 2) {
        // raw_path may be in canonical form with backslashes converted earlier only for POSIX style.
        // Recognize both //server/share and \\server\share patterns.
        if ((raw_path[0] == '/' && raw_path[1] == '/') ||
            (raw_path[0] == '\\' && raw_path[1] == '\\')) {
          is_unc = true;
        }
      }
      if (is_unc) {
        // If it's UNC, normalize to backslash form and keep as-is relative to network root.
        std::string unc{ raw_path };
        for (auto &ch : unc) {
          if (ch == '/') ch = '\\';
        }
        resolved = std::filesystem::path(unc);
        return resolved.lexically_normal();
      }
      // Determine current drive (e.g., C:) from current_path.
      auto const current = std::filesystem::current_path();
      std::string drive_prefix;
      if (current.has_root_name()) {
        drive_prefix = current.root_name().string(); // e.g., "C:"
      } else {
        drive_prefix = "C:"; // Fallback; unlikely but safe default.
      }
      // If raw_path is just a single slash/backslash treat as root of drive.
      if (raw_path.size() == 1) {
        resolved = std::filesystem::path(drive_prefix + "\\");
      } else {
        // Strip leading slashes/backslashes.
        size_t first_non = 0;
        while (first_non < raw_path.size() && (raw_path[first_non] == '/' || raw_path[first_non] == '\\')) {
          ++first_non;
        }
        auto tail = raw_path.substr(first_non);
        resolved = std::filesystem::path(drive_prefix + "\\" + tail);
      }
    }
  }
#endif

  return resolved.lexically_normal();
}

}  // namespace envy
