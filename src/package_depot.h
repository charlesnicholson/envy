#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace envy {

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

  // Returns archive URL if an exact match is found.
  // Searches manifests in order; stops at first manifest with a match.
  std::optional<std::string> find(std::string_view identity,
                                  std::string_view platform,
                                  std::string_view arch,
                                  std::string_view hash_prefix) const;

  bool empty() const;

 private:
  // Each manifest's entries: filename stem → archive URL
  std::vector<std::unordered_map<std::string, std::string>> manifests_;
};

}  // namespace envy
