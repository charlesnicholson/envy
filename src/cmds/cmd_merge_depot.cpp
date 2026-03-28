#include "cmd_merge_depot.h"

#include "fetch.h"
#include "platform.h"
#include "tui.h"
#include "uri.h"

#include "CLI11.hpp"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace envy {
namespace {

bool is_hex_char(char c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

std::vector<depot_manifest_entry> parse_manifest_lines(std::istream &in) {
  std::vector<depot_manifest_entry> entries;
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') { line.pop_back(); }
    if (line.empty() || line[0] == '#') { continue; }

    if (line.size() <= 66 || line[64] != ' ' || line[65] != ' ') {
      tui::warn("merge-depot: skipping malformed line: %s", line.c_str());
      continue;
    }

    bool all_hex{ true };
    for (size_t i{ 0 }; i < 64; ++i) {
      if (!is_hex_char(line[i])) {
        all_hex = false;
        break;
      }
    }
    if (!all_hex) {
      tui::warn("merge-depot: skipping malformed line: %s", line.c_str());
      continue;
    }

    std::string hash{ line.substr(0, 64) };
    for (auto &c : hash) {
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    entries.push_back(depot_manifest_entry{ std::move(hash), line.substr(66) });
  }
  return entries;
}

std::unordered_set<std::string> parse_retain_lines(std::istream &in) {
  std::unordered_set<std::string> paths;
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') { line.pop_back(); }
    if (line.empty() || line[0] == '#') { continue; }
    paths.insert(std::move(line));
  }
  return paths;
}

}  // namespace

std::vector<depot_manifest_entry> parse_depot_manifest(std::filesystem::path const &file) {
  std::ifstream in{ file };
  if (!in) {
    throw std::runtime_error("merge-depot: cannot open depot manifest: " + file.string());
  }
  return parse_manifest_lines(in);
}

void cmd_merge_depot::register_cli(CLI::App &app, std::function<void(cfg)> on_selected) {
  auto *sub{ app.add_subcommand("merge-depot", "Merge depot manifest files") };
  auto cfg_ptr{ std::make_shared<cfg>() };
  sub->add_option("depot_manifests",
                  cfg_ptr->depot_manifests,
                  "Per-OS depot manifest files to merge")
      ->required()
      ->check(CLI::ExistingFile);
  sub->add_option("--existing",
                  cfg_ptr->existing_path,
                  "Existing merged depot manifest (local path or remote URL)");
  sub->add_option("--retain",
                  cfg_ptr->retain_path,
                  "Retain list: prune entries whose path is absent");
  sub->add_flag("--strict",
                cfg_ptr->strict,
                "Treat hash changes vs existing depot manifest as errors");
  sub->callback(
      [cfg_ptr, on_selected = std::move(on_selected)] { on_selected(*cfg_ptr); });
}

cmd_merge_depot::cmd_merge_depot(
    cmd_merge_depot::cfg cfg,
    std::optional<std::filesystem::path> const & /*cli_cache_root*/)
    : cfg_{ std::move(cfg) } {}

void cmd_merge_depot::execute() {
  // merged: path -> hash, using std::map for sorted iteration
  std::map<std::string, std::string> merged;

  // Track original hashes from --existing for conflict messages
  std::map<std::string, std::string> existing_hashes;

  // 1. Load existing depot manifest if provided
  if (cfg_.existing_path) {
    std::vector<depot_manifest_entry> existing_entries;

    if (auto const info{ uri_classify(*cfg_.existing_path) };
        info.scheme == uri_scheme::LOCAL_FILE_ABSOLUTE ||
        info.scheme == uri_scheme::LOCAL_FILE_RELATIVE) {
      std::filesystem::path p{ info.canonical };
      if (!std::filesystem::exists(p)) {
        throw std::runtime_error("merge-depot: --existing file not found: " +
                                 *cfg_.existing_path);
      }
      existing_entries = parse_depot_manifest(p);
    } else {
      // Fetch remote manifest to unique temp file, read into memory, clean up, parse
      scoped_path_cleanup tmp_guard{
          platform::create_unique_temp_file("envy-merge-depot") };

      auto req{ fetch_request_from_url(*cfg_.existing_path, tmp_guard.path()) };
      auto results{ fetch({ req }) };
      if (auto const *err{ std::get_if<std::string>(&results[0]) }) {
        throw std::runtime_error("merge-depot: failed to fetch --existing: " + *err);
      }

      auto const content{ [&] {
        std::ifstream in{ tmp_guard.path() };
        if (!in) {
          throw std::runtime_error(
              "merge-depot: failed to read fetched --existing manifest");
        }
        return std::string{ std::istreambuf_iterator<char>{ in },
                            std::istreambuf_iterator<char>{} };
      }() };

      auto existing_stream{ std::istringstream{ content } };
      existing_entries = parse_manifest_lines(existing_stream);
    }

    for (auto &e : existing_entries) {
      if (auto [it, inserted]{ merged.emplace(e.path, e.hash) }; inserted) {
        existing_hashes.emplace(std::move(e.path), std::move(e.hash));
      } else {
        tui::warn("merge-depot: duplicate path in existing manifest: %s", e.path.c_str());
      }
    }
  }

  // Track paths introduced/modified by new manifests for cross-input detection
  std::set<std::string> new_paths;

  // 2. Layer each new depot manifest
  for (auto const &manifest_path : cfg_.depot_manifests) {
    for (auto &e : parse_depot_manifest(manifest_path)) {
      if (auto it{ merged.find(e.path) }; it != merged.end() && it->second != e.hash) {
        if (new_paths.count(e.path) > 0) {
          // Path already modified by a prior new manifest — cross-input conflict
          throw std::runtime_error("merge-depot: conflicting hashes for " + e.path +
                                   " across input depot manifests");
        }

        // Hash changed vs existing depot manifest
        if (cfg_.strict) {
          auto ex_it{ existing_hashes.find(e.path) };
          throw std::runtime_error("merge-depot: hash changed for " + e.path +
                                   " (existing: " + ex_it->second + ", new: " + e.hash +
                                   ")");
        }
        tui::warn("merge-depot: hash changed for %s", e.path.c_str());
      }

      new_paths.insert(e.path);
      merged.insert_or_assign(std::move(e.path), std::move(e.hash));
    }
  }

  // 3. Apply --retain pruning
  if (cfg_.retain_path) {
    std::unordered_set<std::string> retain_set;

    if (auto const info{ uri_classify(*cfg_.retain_path) };
        info.scheme == uri_scheme::LOCAL_FILE_ABSOLUTE ||
        info.scheme == uri_scheme::LOCAL_FILE_RELATIVE) {
      std::filesystem::path p{ info.canonical };
      if (!std::filesystem::exists(p)) {
        throw std::runtime_error("merge-depot: --retain file not found: " +
                                 *cfg_.retain_path);
      }
      std::ifstream in{ p };
      if (!in) {
        throw std::runtime_error("merge-depot: cannot open --retain file: " +
                                 *cfg_.retain_path);
      }
      retain_set = parse_retain_lines(in);
    } else {
      scoped_path_cleanup tmp_guard{
          platform::create_unique_temp_file("envy-merge-depot-retain") };

      auto req{ fetch_request_from_url(*cfg_.retain_path, tmp_guard.path()) };
      auto results{ fetch({ req }) };
      if (auto const *err{ std::get_if<std::string>(&results[0]) }) {
        throw std::runtime_error("merge-depot: failed to fetch --retain: " + *err);
      }

      auto const content{ [&] {
        std::ifstream in{ tmp_guard.path() };
        if (!in) {
          throw std::runtime_error("merge-depot: failed to read fetched --retain list");
        }
        return std::string{ std::istreambuf_iterator<char>{ in },
                            std::istreambuf_iterator<char>{} };
      }() };

      auto retain_stream{ std::istringstream{ content } };
      retain_set = parse_retain_lines(retain_stream);
    }

    for (auto it{ merged.begin() }; it != merged.end();) {
      if (retain_set.count(it->first) == 0 && new_paths.count(it->first) == 0) {
        it = merged.erase(it);
      } else {
        ++it;
      }
    }
  }

  // 4. Output sorted depot manifest
  for (auto const &[path, hash] : merged) {
    tui::print_stdout("%s  %s\n", hash.c_str(), path.c_str());
  }
}

}  // namespace envy
