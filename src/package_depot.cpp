#include "package_depot.h"

#include "cache.h"
#include "fetch.h"
#include "tui.h"
#include "util.h"

#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

namespace envy {

namespace {

// Parse a single manifest text into a map of filename stem → archive URL.
std::unordered_map<std::string, std::string> parse_manifest_text(std::string_view text) {
  std::unordered_map<std::string, std::string> entries;

  std::istringstream stream{ std::string{ text } };
  std::string line;

  while (std::getline(stream, line)) {
    // Trim trailing \r for Windows line endings
    if (!line.empty() && line.back() == '\r') { line.pop_back(); }

    // Skip blank lines and comments
    if (line.empty()) { continue; }
    if (line[0] == '#') { continue; }

    // Extract filename from URL: everything after the last '/'
    auto const slash_pos{ line.rfind('/') };
    std::string_view filename;
    if (slash_pos != std::string::npos && slash_pos + 1 < line.size()) {
      filename = std::string_view{ line }.substr(slash_pos + 1);
    } else {
      filename = line;
    }

    // Strip .tar.zst extension
    std::string_view stem{ filename };
    if (stem.size() > 8 && stem.substr(stem.size() - 8) == ".tar.zst") {
      stem = stem.substr(0, stem.size() - 8);
    } else {
      tui::warn("depot: skipping line without .tar.zst extension: %s",
                std::string(line).c_str());
      continue;
    }

    if (!util_parse_archive_filename(stem)) {
      tui::warn("depot: skipping unparseable line: %s", std::string(line).c_str());
      continue;
    }

    entries.try_emplace(std::string(stem), line);
  }

  return entries;
}

}  // namespace

package_depot_index package_depot_index::build(std::vector<std::string> const &depot_urls,
                                               std::filesystem::path const &tmp_dir) {
  if (depot_urls.empty()) { return {}; }

  // Download all depot manifests in parallel
  struct manifest_download {
    std::string url;
    std::filesystem::path dest;
    std::string content;
    bool ok{ false };
  };

  std::vector<manifest_download> downloads;
  downloads.reserve(depot_urls.size());

  std::vector<fetch_request> requests;
  requests.reserve(depot_urls.size());

  for (size_t i{ 0 }; i < depot_urls.size(); ++i) {
    auto &dl{ downloads.emplace_back() };
    dl.url = depot_urls[i];
    dl.dest = tmp_dir / ("depot-manifest-" + std::to_string(i) + ".txt");

    try {
      requests.push_back(fetch_request_from_url(dl.url, dl.dest));
    } catch (std::exception const &) {
      tui::warn("depot: unsupported scheme for depot manifest: %s", dl.url.c_str());
    }
  }

  if (!requests.empty()) {
    auto const results{ fetch(requests) };

    size_t req_idx{ 0 };
    for (size_t i{ 0 }; i < downloads.size(); ++i) {
      if (req_idx >= results.size()) { break; }

      // Match downloads to results (skipped unsupported schemes don't produce results)
      auto const *result{ std::get_if<fetch_result>(&results[req_idx]) };
      if (result) {
        // Read downloaded content
        try {
          auto const data{ util_load_file(downloads[i].dest) };
          downloads[i].content.assign(reinterpret_cast<char const *>(data.data()),
                                      data.size());
          downloads[i].ok = true;
        } catch (std::exception const &e) {
          tui::warn("depot: failed to read manifest %s: %s",
                    downloads[i].url.c_str(),
                    e.what());
        }
      } else {
        auto const *error{ std::get_if<std::string>(&results[req_idx]) };
        tui::warn("depot: failed to fetch manifest %s: %s",
                  downloads[i].url.c_str(),
                  error ? error->c_str() : "unknown error");
      }
      ++req_idx;
    }
  }

  // Parse all downloaded manifests
  package_depot_index index;
  for (auto const &dl : downloads) {
    if (!dl.ok) { continue; }
    auto entries{ parse_manifest_text(dl.content) };
    if (!entries.empty()) { index.manifests_.push_back(std::move(entries)); }
  }

  return index;
}

package_depot_index package_depot_index::build_from_contents(
    std::vector<std::string> const &manifest_contents) {
  package_depot_index index;
  for (auto const &content : manifest_contents) {
    auto entries{ parse_manifest_text(content) };
    if (!entries.empty()) { index.manifests_.push_back(std::move(entries)); }
  }
  return index;
}

std::optional<std::string> package_depot_index::find(std::string_view identity,
                                                     std::string_view platform,
                                                     std::string_view arch,
                                                     std::string_view hash_prefix) const {
  auto const key{ cache::key(identity, platform, arch, hash_prefix) };

  // Search manifests in order; stop at first match
  for (auto const &manifest : manifests_) {
    auto const it{ manifest.find(key) };
    if (it != manifest.end()) { return it->second; }
  }

  return std::nullopt;
}

bool package_depot_index::empty() const { return manifests_.empty(); }

}  // namespace envy
