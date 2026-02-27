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

#include "CLI11.hpp"

#include <memory>
#include <stdexcept>
#include <string>

namespace envy {
namespace {

void export_one_package(pkg *p, std::filesystem::path const &output_dir) {
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

  std::string const variant{ entry_dir.filename().string() };
  std::string const filename{ std::string(p->key.identity()) + "-" + variant +
                              ".tar.zst" };

  std::filesystem::path const output_path{ output_dir / filename };

  archive_create_tar_zst(output_path, source_dir, prefix);
  tui::print_stdout("%s\n", output_path.string().c_str());
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

  for (auto const *cfg : targets) {
    pkg_key const key{ *cfg };
    pkg *p{ eng.find_exact(key) };
    if (!p) {
      throw std::runtime_error("export: spec not found in graph for " +
                               std::string(key.identity()));
    }
    export_one_package(p, output_dir);
  }
}

}  // namespace envy
