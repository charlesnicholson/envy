#include "cmd_install.h"

#include "cache.h"
#include "engine.h"
#include "manifest.h"
#include "pkg_key.h"

#include "CLI11.hpp"

#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace envy {

namespace fs = std::filesystem;

void cmd_install::register_cli(CLI::App &app, std::function<void(cfg)> on_selected) {
  auto *sub{ app.add_subcommand("install", "Install packages from manifest") };
  auto cfg_ptr{ std::make_shared<cfg>() };
  sub->add_option("queries",
                  cfg_ptr->queries,
                  "Package queries to install (install all if omitted)");
  sub->add_option("--manifest", cfg_ptr->manifest_path, "Path to envy.lua manifest");
  sub->callback(
      [cfg_ptr, on_selected = std::move(on_selected)] { on_selected(*cfg_ptr); });
}

cmd_install::cmd_install(cfg cfg, std::optional<fs::path> const &cli_cache_root)
    : cfg_{ std::move(cfg) }, cli_cache_root_{ cli_cache_root } {}

void cmd_install::execute() {
  auto const m{ manifest::load(manifest::find_manifest_path(cfg_.manifest_path, false)) };
  if (!m) { throw std::runtime_error("install: could not load manifest"); }

  auto c{ cache::ensure(cli_cache_root_, m->meta.cache) };

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
        throw std::runtime_error("install: query '" + query + "' not found in manifest");
      }
    }
  }

  if (targets.empty()) { return; }

  engine eng{ *c, m.get() };
  auto result{ eng.run_full(targets) };

  size_t failed{ 0 };
  for (auto const &[key, outcome] : result) {
    if (outcome.type == pkg_type::UNKNOWN) { ++failed; }
  }

  if (failed > 0) {
    throw std::runtime_error("install: " + std::to_string(failed) + " package(s) failed");
  }
}

}  // namespace envy
