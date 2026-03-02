#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace envy {

struct depot_entry {
  std::string url;
  std::optional<std::string> sha256;  // lowercase 64-char hex, or nullopt
};

// Index of pre-built package archives available from remote depots.
// Depot manifests are searched in order; the first manifest containing
// a match for a given package wins (subsequent manifests are not consulted).
class package_depot_index {
 public:
  // Build index by downloading depot manifest text files and parsing entries.
  // Failed downloads / unparseable lines are warned and skipped.
  static package_depot_index build(std::vector<std::string> const &depot_urls,
                                   std::filesystem::path const &tmp_dir);

  // Build index from pre-fetched manifest content strings (for testing).
  static package_depot_index build_from_contents(
      std::vector<std::string> const &manifest_contents);

  // Build index from a local directory of .tar.zst archives.
  // Scans dir for .tar.zst files, maps filename stems → absolute file paths.
  static package_depot_index build_from_directory(std::filesystem::path const &dir);

  // Build from directory with accompanying checksums (filename → sha256 hex).
  static package_depot_index build_from_directory(
      std::filesystem::path const &dir,
      std::unordered_map<std::string, std::string> const &checksums);

  // Returns depot entry if an exact match is found.
  // Searches manifests in order; stops at first manifest with a match.
  std::optional<depot_entry> find(std::string_view identity,
                                  std::string_view platform,
                                  std::string_view arch,
                                  std::string_view hash_prefix) const;

  bool empty() const;

 private:
  // Each manifest's entries: filename stem → depot entry (URL + optional SHA256)
  std::vector<std::unordered_map<std::string, depot_entry>> manifests_;
};

}  // namespace envy
