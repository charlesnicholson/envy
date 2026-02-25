#include "cmd_deploy.h"

#include "bootstrap.h"
#include "deploy.h"
#include "engine.h"
#include "luarc.h"
#include "manifest.h"
#include "pkg_cfg.h"
#include "pkg_key.h"
#include "reexec.h"
#include "self_deploy.h"
#include "tui.h"
#include "util.h"

#include "CLI11.hpp"

#include <filesystem>
#include <memory>
#include <stdexcept>

namespace envy {

namespace fs = std::filesystem;

void cmd_deploy::register_cli(CLI::App &app, std::function<void(cfg)> on_selected) {
  auto *sub{ app.add_subcommand("deploy", "Deploy product scripts") };
  auto cfg_ptr{ std::make_shared<cfg>() };
  sub->add_option("identities",
                  cfg_ptr->identities,
                  "Spec identities to deploy (deploy all if omitted)");
  auto *manifest_opt{
    sub->add_option("--manifest", cfg_ptr->manifest_path, "Path to envy.lua manifest")
  };
  sub->add_flag("--strict",
                cfg_ptr->strict,
                "Error on non-envy-managed product script conflicts");
  sub->add_flag("--subproject",
                cfg_ptr->subproject,
                "Use nearest manifest instead of walking to root")
      ->excludes(manifest_opt);
  sub->add_option("--platform",
                  cfg_ptr->platform_flag,
                  "Script platform: posix, windows, or all (default: current OS)")
      ->check(CLI::IsMember({ "posix", "windows", "all" }));
  sub->callback(
      [cfg_ptr, on_selected = std::move(on_selected)] { on_selected(*cfg_ptr); });
}

cmd_deploy::cmd_deploy(cfg cfg, std::optional<std::filesystem::path> const &cli_cache_root)
    : cfg_{ std::move(cfg) }, cli_cache_root_{ cli_cache_root } {}

void cmd_deploy::execute() {
  auto const m{ manifest::load(
      manifest::find_manifest_path(cfg_.manifest_path, cfg_.subproject)) };
  if (!m) { throw std::runtime_error("deploy: could not load manifest"); }

  reexec_if_needed(m->meta, cli_cache_root_);

  if (!m->meta.bin) {
    throw std::runtime_error(
        "deploy: manifest missing '@envy bin' directive (required for deploy)");
  }

  auto const platforms{ util_parse_platform_flag(cfg_.platform_flag) };

  auto c{ self_deploy::ensure(cli_cache_root_, m->meta.cache) };

  fs::path const manifest_dir{ m->manifest_path.parent_path() };
  update_luarc_types_path(manifest_dir, c->root());

  fs::path const bin_dir{ manifest_dir / *m->meta.bin };

  if (!fs::exists(bin_dir)) {
    std::error_code ec;
    fs::create_directories(bin_dir, ec);
    if (ec) {
      throw std::runtime_error("deploy: failed to create bin directory " +
                               bin_dir.string() + ": " + ec.message());
    }
  }

  std::vector<pkg_cfg const *> targets;

  if (cfg_.identities.empty()) {
    for (auto const *pkg : m->packages) { targets.push_back(pkg); }
  } else {
    for (auto const &query : cfg_.identities) {
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
        throw std::runtime_error("deploy: query '" + query + "' not found in manifest");
      }
    }
  }

  if (targets.empty()) { return; }

  engine eng{ *c, m.get() };
  eng.resolve_graph(targets);

  auto const products{ eng.collect_all_products() };

  // Update bootstrap script (always, regardless of deploy setting)
  for (auto const plat : platforms) {
    if (bootstrap_write_script(bin_dir, m->meta.mirror, plat)) {
      tui::info("Updated bootstrap script");
    }
  }

  // Check deploy directive: absent or false means deployment disabled
  bool const deploy_enabled{ m->meta.deploy.has_value() && *m->meta.deploy };

  if (deploy_enabled) {
    deploy_product_scripts(eng, bin_dir, products, cfg_.strict, platforms);
  } else {
    tui::warn("deploy was requested but deployment is disabled in %s",
              m->manifest_path.string().c_str());
    tui::info("Add '-- @envy deploy \"true\"' to enable product script deployment");
  }
}

}  // namespace envy
