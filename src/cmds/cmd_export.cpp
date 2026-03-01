#include "cmd_export.h"

#include "engine.h"
#include "extract.h"
#include "manifest.h"
#include "pkg.h"
#include "pkg_cfg.h"
#include "pkg_key.h"
#include "reexec.h"
#include "self_deploy.h"
#include "tui.h"
#include "util.h"

#include "CLI11.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

namespace envy {
namespace {

void export_one_package(pkg *p,
                        std::filesystem::path const &output_dir,
                        std::optional<std::string> const &depot_prefix) {
  if (p->type != pkg_type::CACHE_MANAGED) {
    tui::warn("export: skipping non-cache-managed package %s",
              std::string(p->key.identity()).c_str());
    return;
  }

  sol::state_view lua{ *p->lua };
  sol::object exportable_obj{ lua["EXPORTABLE"] };
  bool const exportable{ exportable_obj.valid() &&
                         exportable_obj.get_type() == sol::type::boolean &&
                         exportable_obj.as<bool>() };

  // Determine source directory and archive prefix
  std::filesystem::path const entry_dir{ p->pkg_path.parent_path() };
  std::string const prefix{ exportable ? "pkg" : "fetch" };
  std::filesystem::path const source_dir{ entry_dir / prefix };

  {
    std::error_code ec;
    std::filesystem::directory_iterator it{ source_dir, ec };
    if (ec || it == std::filesystem::directory_iterator{}) {
      throw std::runtime_error(
          "export: " + prefix + "/ directory is empty or missing for " +
          std::string(p->key.identity()) + " (package may predate export support)");
    }
  }

  std::string const filename{ std::string(p->key.identity()) + "-" +
                              entry_dir.filename().string() + ".tar.zst" };
  std::filesystem::path const output_path{ output_dir / filename };

  auto const section{ tui::section_create() };
  std::string const label{ "[" + std::string(p->key.identity()) + "]" };

  // Scan source to compute totals
  tui::section_set_content(
      section,
      tui::section_frame{ .label = label,
                          .content = tui::spinner_data{
                              .text = "scanning...",
                              .start_time = std::chrono::steady_clock::now() } });

  std::uint64_t total_bytes{ 0 };
  std::uint64_t total_files{ 0 };
  for (auto const &e : std::filesystem::recursive_directory_iterator(source_dir)) {
    if (e.is_regular_file()) {
      total_bytes += e.file_size();
      ++total_files;
    }
  }

  // Compress with progress
  archive_create_tar_zst(
      output_path,
      source_dir,
      prefix,
      [&](extract_progress const &ep) -> bool {
        double percent{ 0.0 };
        if (total_bytes > 0) {
          percent =
              std::min(100.0,
                       (ep.bytes_processed / static_cast<double>(total_bytes)) * 100.0);
        } else if (total_files > 0) {
          percent =
              std::min(100.0,
                       (ep.files_processed / static_cast<double>(total_files)) * 100.0);
        }

        std::ostringstream status;
        status << ep.files_processed;
        if (total_files > 0) { status << "/" << total_files; }
        status << " files";
        if (total_bytes > 0) {
          status << " " << util_format_bytes(ep.bytes_processed) << "/"
                 << util_format_bytes(total_bytes);
        }

        tui::section_set_content(
            section,
            tui::section_frame{ .label = label,
                                .content = tui::progress_data{ .percent = percent,
                                                               .status = status.str() } });
        return true;
      });

  tui::section_set_content(
      section,
      tui::section_frame{ .label = label,
                          .content = tui::static_text_data{ .text = "done" } });
  tui::section_set_complete(section);

  if (depot_prefix) {
    tui::print_stdout("%s%s\n", depot_prefix->c_str(), filename.c_str());
  } else {
    tui::print_stdout("%s\n", output_path.string().c_str());
  }
}

}  // namespace

void cmd_export::register_cli(CLI::App &app, std::function<void(cfg)> on_selected) {
  auto *sub{ app.add_subcommand("export", "Export cached packages as tar.zst archives") };
  auto cfg_ptr{ std::make_shared<cfg>() };
  sub->add_option("queries",
                  cfg_ptr->queries,
                  "Package queries to export (export all if omitted)");
  sub->add_option("-o,--output-dir", cfg_ptr->output_dir, "Output directory for archives");
  sub->add_option("--manifest", cfg_ptr->manifest_path, "Path to envy.lua manifest");
  sub->add_option("--depot-prefix",
                  cfg_ptr->depot_prefix,
                  "URL prefix for depot manifest output");
  sub->callback(
      [cfg_ptr, on_selected = std::move(on_selected)] { on_selected(*cfg_ptr); });
}

cmd_export::cmd_export(cfg cfg, std::optional<std::filesystem::path> const &cli_cache_root)
    : cfg_{ std::move(cfg) }, cli_cache_root_{ cli_cache_root } {}

void cmd_export::execute() {
  auto const m{ manifest::find_and_load(cfg_.manifest_path) };

  reexec_if_needed(m->meta, cli_cache_root_);

  auto c{ self_deploy::ensure(cli_cache_root_, m->meta.cache) };

  // Collect target packages: all if no queries, matched subset otherwise
  std::vector<pkg_cfg const *> targets;

  if (cfg_.queries.empty()) {
    for (auto const *pkg : m->packages) { targets.push_back(pkg); }
  } else {
    for (auto const &query : cfg_.queries) {
      bool found{ false };
      for (auto const *pkg : m->packages) {
        pkg_key const key{ *pkg };
        if (key.matches(query)) {
          targets.push_back(pkg);
          found = true;
          break;
        }
      }
      if (!found) {
        throw std::runtime_error("export: no package matching '" + query + "'");
      }
    }
  }

  if (targets.empty()) { return; }

  engine eng{ *c, m.get() };

  std::vector<pkg_cfg const *> roots;
  roots.reserve(m->packages.size());
  for (auto *pkg : m->packages) { roots.push_back(pkg); }

  eng.resolve_graph(roots);

  // Ensure all target packages are fully installed
  for (auto const *cfg : targets) {
    pkg_key const key{ *cfg };
    eng.ensure_pkg_at_phase(key, pkg_phase::completion);
  }

  std::filesystem::path const output_dir{ cfg_.output_dir
                                              ? *cfg_.output_dir
                                              : std::filesystem::current_path() };

  {
    std::error_code ec;
    std::filesystem::create_directories(output_dir, ec);
    if (ec) {
      throw std::runtime_error("export: failed to create output directory " +
                               output_dir.string() + ": " + ec.message());
    }
  }

  std::vector<std::thread> workers;
  std::atomic_bool had_error{ false };
  std::mutex error_mutex;
  std::string first_error;

  workers.reserve(targets.size());
  for (auto const *cfg : targets) {
    pkg_key const key{ *cfg };
    pkg *p{ eng.find_exact(key) };
    if (!p) {
      throw std::runtime_error("export: spec not found in graph for " +
                               std::string(key.identity()));
    }
    workers.emplace_back([p, &output_dir, &had_error, &error_mutex, &first_error, this] {
      try {
        export_one_package(p, output_dir, cfg_.depot_prefix);
      } catch (std::exception const &e) {
        std::lock_guard lock{ error_mutex };
        if (!had_error.exchange(true)) { first_error = e.what(); }
      }
    });
  }

  for (auto &w : workers) { w.join(); }
  if (had_error) { throw std::runtime_error(first_error); }
}

}  // namespace envy
