#include "cmd_import.h"

#include "cache.h"
#include "extract.h"
#include "tui.h"
#include "util.h"

#include "CLI11.hpp"

#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

namespace envy {
namespace {

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

  auto const parsed{ util_parse_archive_filename(stem) };
  if (!parsed) {
    throw std::runtime_error(
        "import: invalid archive filename, expected "
        "<identity>-<platform>-<arch>-blake3-<hash>.tar.zst");
  }

  cache c{ cli_cache_root_ };
  auto result{
    c.ensure_pkg(parsed->identity, parsed->platform, parsed->arch, parsed->hash_prefix)
  };

  if (!result.lock) {  // Already cached
    tui::print_stdout("%s\n", result.pkg_path.string().c_str());
    return;
  }

  extract(cfg_.archive_path, result.entry_path);

  if (directory_has_entries(result.lock->install_dir())) {  // Full import, complete
    result.lock->mark_install_complete();
    tui::print_stdout("%s\n", result.pkg_path.string().c_str());
  } else if (directory_has_entries(result.lock->fetch_dir())) {
    // Fetch-only import — mark fetch complete so cache state is consistent.
    result.lock->mark_fetch_complete();
    tui::print_stdout("fetch-only import: %s\n", result.entry_path.string().c_str());
  } else {
    throw std::runtime_error(
        "import: archive did not populate pkg/ or fetch/ directories");
  }
}

}  // namespace envy
