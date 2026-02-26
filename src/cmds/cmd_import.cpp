#include "cmd_import.h"

#include "cache.h"
#include "extract.h"
#include "tui.h"

#include "CLI11.hpp"

#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

namespace envy {
namespace {

struct import_parsed_filename {
  std::string identity;
  std::string platform;
  std::string arch;
  std::string hash_prefix;
};

import_parsed_filename parse_filename(std::string_view stem) {
  auto const at_pos{ stem.find('@') };
  if (at_pos == std::string_view::npos) {
    throw std::runtime_error("import: invalid archive filename, missing '@'");
  }

  // From '@', find the next '-' after the revision digits
  auto const after_at{ stem.substr(at_pos + 1) };
  auto const dash_pos{ after_at.find('-') };
  if (dash_pos == std::string_view::npos) {
    throw std::runtime_error("import: invalid archive filename, missing variant");
  }

  auto const identity_end{ at_pos + 1 + dash_pos };
  std::string const identity{ stem.substr(0, identity_end) };
  std::string_view const variant{ stem.substr(identity_end + 1) };

  // variant = platform-arch-blake3-hash_prefix
  // Split by '-': [platform, arch, "blake3", hash_prefix]
  std::string_view remaining{ variant };
  auto split_next = [&]() -> std::string {
    auto const pos{ remaining.find('-') };
    if (pos == std::string_view::npos) {
      std::string result{ remaining };
      remaining = {};
      return result;
    }
    std::string result{ remaining.substr(0, pos) };
    remaining = remaining.substr(pos + 1);
    return result;
  };

  std::string const platform{ split_next() };
  std::string const arch{ split_next() };
  std::string const blake3_tag{ split_next() };
  std::string const hash_prefix{ std::string(remaining) };

  if (platform.empty() || arch.empty() || blake3_tag != "blake3" || hash_prefix.empty()) {
    throw std::runtime_error(
        "import: invalid archive filename, expected "
        "<identity>-<platform>-<arch>-blake3-<hash>.tar.zst");
  }

  return { identity, platform, arch, hash_prefix };
}

bool directory_has_entries(std::filesystem::path const &dir) {
  std::error_code ec;
  std::filesystem::directory_iterator it{ dir, ec };
  if (ec) { return false; }
  return it != std::filesystem::directory_iterator{};
}

}  // namespace

void cmd_import::register_cli(CLI::App &app, std::function<void(cfg)> on_selected) {
  auto *sub{ app.add_subcommand("import", "Import package archive into cache") };
  auto cfg_ptr{ std::make_shared<cfg>() };
  sub->add_option("archive", cfg_ptr->archive_path, "Path to .tar.zst archive")
      ->required()
      ->check(CLI::ExistingFile);
  sub->callback(
      [cfg_ptr, on_selected = std::move(on_selected)] { on_selected(*cfg_ptr); });
}

cmd_import::cmd_import(cfg cfg, std::optional<std::filesystem::path> const &cli_cache_root)
    : cfg_{ std::move(cfg) }, cli_cache_root_{ cli_cache_root } {}

void cmd_import::execute() {
  std::string filename{ cfg_.archive_path.filename().string() };

  // Strip .tar.zst suffix
  std::string_view stem{ filename };
  if (stem.size() > 8 && stem.substr(stem.size() - 8) == ".tar.zst") {
    stem = stem.substr(0, stem.size() - 8);
  } else {
    throw std::runtime_error("import: archive must have .tar.zst extension");
  }

  auto const parsed{ parse_filename(stem) };

  cache c{ cli_cache_root_ };
  auto result{
    c.ensure_pkg(parsed.identity, parsed.platform, parsed.arch, parsed.hash_prefix)
  };

  if (!result.lock) {  // Already cached
    tui::print_stdout("%s\n", result.pkg_path.string().c_str());
    return;
  }

  extract(cfg_.archive_path, result.entry_path);

  if (directory_has_entries(result.lock->install_dir())) {  // Full import, complete
    result.lock->mark_install_complete();
    tui::print_stdout("%s\n", result.pkg_path.string().c_str());
  } else if (result.lock->is_fetch_complete()) {
    // Fetch-only import — don't mark install complete.
    // Destructor failure path preserves non-empty fetch/.
    tui::print_stdout("fetch-only import: %s\n", result.entry_path.string().c_str());
  } else {
    throw std::runtime_error(
        "import: archive did not populate pkg/ or fetch/ directories");
  }
}

#ifdef ENVY_UNIT_TEST
parsed_export_filename parse_export_filename(std::string_view stem) {
  auto const r{ parse_filename(stem) };
  return { r.identity, r.platform, r.arch, r.hash_prefix };
}
#endif

}  // namespace envy
