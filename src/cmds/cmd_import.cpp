#include "cmd_import.h"

#include "cache.h"
#include "engine.h"
#include "extract.h"
#include "manifest.h"
#include "package_depot.h"
#include "pkg_cfg.h"
#include "reexec.h"
#include "self_deploy.h"
#include "tui.h"
#include "util.h"

#include "CLI11.hpp"

#include <chrono>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace envy {
namespace {

bool directory_has_entries(std::filesystem::path const &dir) {
  std::error_code ec;
  std::filesystem::directory_iterator it{ dir, ec };
  if (ec) { return false; }
  return it != std::filesystem::directory_iterator{};
}

struct import_result {
  std::string identity;
  std::filesystem::path pkg_path;
  bool was_cached;
  bool is_fetch_only;
};

import_result import_one_archive(cache &c,
                                 std::filesystem::path const &archive_path,
                                 tui::section_handle section = tui::kInvalidSection) {
  std::string filename{ archive_path.filename().string() };

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
        "<identity>@<revision>-<platform>-<arch>-blake3-<hash_prefix>.tar.zst");
  }

  std::string const label{ "[" + parsed->identity + "]" };

  auto result{
    c.ensure_pkg(parsed->identity, parsed->platform, parsed->arch, parsed->hash_prefix)
  };

  if (!result.lock) {
    if (section) {
      tui::section_set_content(
          section,
          tui::section_frame{ .label = label,
                              .content = tui::static_text_data{ .text = "cached" } });
      tui::section_set_complete(section);
    }
    return { parsed->identity, result.pkg_path, true, false };
  }

  // Compute archive size for progress reporting
  std::uint64_t const archive_bytes{ std::filesystem::file_size(archive_path) };

  if (section) {
    tui::section_set_content(
        section,
        tui::section_frame{ .label = label,
                            .content = tui::spinner_data{
                                .text = "extracting...",
                                .start_time = std::chrono::steady_clock::now() } });
  }

  extract_options opts;
  if (section) {
    opts.progress = [&](extract_progress const &ep) -> bool {
      double percent{ 0.0 };
      if (archive_bytes > 0) {
        percent =
            std::min(100.0,
                     (ep.bytes_processed / static_cast<double>(archive_bytes)) * 100.0);
      }

      std::ostringstream status;
      status << ep.files_processed << " files";
      if (archive_bytes > 0) { status << " " << util_format_bytes(ep.bytes_processed); }

      tui::section_set_content(
          section,
          tui::section_frame{ .label = label,
                              .content = tui::progress_data{ .percent = percent,
                                                             .status = status.str() } });
      return true;
    };
  }
  extract(archive_path, result.entry_path, opts);

  if (directory_has_entries(result.lock->install_dir())) {
    result.lock->mark_install_complete();
    if (section) {
      tui::section_set_content(
          section,
          tui::section_frame{ .label = label,
                              .content = tui::static_text_data{ .text = "imported" } });
      tui::section_set_complete(section);
    }
    return { parsed->identity, result.pkg_path, false, false };
  }

  if (directory_has_entries(result.lock->fetch_dir())) {
    result.lock->mark_fetch_complete();
    if (section) {
      tui::section_set_content(section,
                               tui::section_frame{ .label = label,
                                                   .content = tui::static_text_data{
                                                       .text = "imported (fetch)" } });
      tui::section_set_complete(section);
    }
    return { parsed->identity, result.entry_path, false, true };
  }

  throw std::runtime_error("import: archive did not populate pkg/ or fetch/ directories");
}

}  // namespace

void cmd_import::register_cli(CLI::App &app, std::function<void(cfg)> on_selected) {
  auto *sub{ app.add_subcommand("import", "Import package archive into cache") };
  auto cfg_ptr{ std::make_shared<cfg>() };
  sub->add_option("archive", cfg_ptr->archive_path, "Path to .tar.zst archive")
      ->check(CLI::ExistingFile);
  sub->add_option("--dir", cfg_ptr->dir, "Directory of .tar.zst archives to import")
      ->check(CLI::ExistingDirectory);
  sub->add_option("--manifest", cfg_ptr->manifest_path, "Path to envy.lua manifest");
  sub->callback([cfg_ptr, on_selected = std::move(on_selected)] {
    bool const has_archive{ !cfg_ptr->archive_path.empty() };
    bool const has_dir{ cfg_ptr->dir.has_value() };
    if (has_archive && has_dir) {
      throw CLI::ValidationError("Cannot specify both archive and --dir");
    }
    if (!has_archive && !has_dir) {
      throw CLI::ValidationError("Must specify either archive or --dir");
    }
    on_selected(*cfg_ptr);
  });
}

cmd_import::cmd_import(cfg cfg, std::optional<std::filesystem::path> const &cli_cache_root)
    : cfg_{ std::move(cfg) }, cli_cache_root_{ cli_cache_root } {}

void cmd_import::execute() {
  if (!cfg_.dir) {
    // Single-file import
    cache c{ cli_cache_root_ };
    auto const section{ tui::section_create() };
    auto result{ import_one_archive(c, cfg_.archive_path, section) };
    if (result.is_fetch_only) {
      tui::print_stdout("fetch-only import: %s\n", result.pkg_path.string().c_str());
    } else {
      tui::print_stdout("%s\n", result.pkg_path.string().c_str());
    }
    return;
  }

  // Directory import — build depot index from directory, let engine handle everything
  auto const m{ manifest::find_and_load(cfg_.manifest_path) };
  if (!m) { throw std::runtime_error("import: could not load manifest"); }

  reexec_if_needed(m->meta, cli_cache_root_);

  auto c{ self_deploy::ensure(cli_cache_root_, m->meta.cache) };

  auto depot{ package_depot_index::build_from_directory(*cfg_.dir) };
  if (depot.empty()) {
    tui::warn("import: no .tar.zst files found in %s", cfg_.dir->string().c_str());
    return;
  }

  engine eng{ *c, m.get() };
  eng.set_depot_index(std::move(depot));

  std::vector<pkg_cfg const *> roots;
  roots.reserve(m->packages.size());
  for (auto *pkg : m->packages) { roots.push_back(pkg); }

  eng.run_full(roots);
}

}  // namespace envy
