#include "cmd_package.h"

#include "cache.h"
#include "cmd_common.h"
#include "engine.h"
#include "manifest.h"
#include "pkg.h"
#include "pkg_cfg.h"
#include "pkg_key.h"
#include "tui.h"

#include "CLI11.hpp"

#include <memory>

namespace envy {

void cmd_package::register_cli(CLI::App &app, std::function<void(cfg)> on_selected) {
  auto *sub{ app.add_subcommand("package",
                                "Query and install package, print package path") };
  auto cfg_ptr{ std::make_shared<cfg>() };
  sub->add_option("identity", cfg_ptr->identity, "Spec identity (namespace.name@version)")
      ->required();
  sub->add_option("--manifest", cfg_ptr->manifest_path, "Path to envy.lua manifest");
  sub->callback(
      [cfg_ptr, on_selected = std::move(on_selected)] { on_selected(*cfg_ptr); });
}

cmd_package::cmd_package(cfg cfg,
                         std::optional<std::filesystem::path> const &cli_cache_root)
    : cfg_{ std::move(cfg) }, cli_cache_root_{ cli_cache_root } {}

void cmd_package::execute() {
  auto const m{ load_manifest_or_throw(cfg_.manifest_path) };

  auto c{ cache::ensure(cli_cache_root_, m->meta.cache) };

  std::vector<pkg_cfg const *> matches;
  for (auto const *pkg : m->packages) {
    if (pkg->identity == cfg_.identity) { matches.push_back(pkg); }
  }

  if (matches.empty()) { throw std::runtime_error("package: identity not found"); }

  if (matches.size() > 1) {
    std::string first_key{ pkg_cfg::format_key(matches[0]->identity,
                                               matches[0]->serialized_options) };
    for (size_t i{ 1 }; i < matches.size(); ++i) {
      std::string key{ pkg_cfg::format_key(matches[i]->identity,
                                           matches[i]->serialized_options) };
      if (key != first_key) {
        throw std::runtime_error("package: identity '" + cfg_.identity +
                                 "' appears multiple times with different options");
      }
    }
  }

  engine eng{ *c, m.get() };

  std::vector<pkg_cfg const *> roots;
  roots.reserve(m->packages.size());
  for (auto *pkg : m->packages) { roots.push_back(pkg); }

  eng.resolve_graph(roots);

  pkg_key const target_key{ *matches[0] };
  pkg *p{ eng.find_exact(target_key) };
  if (!p) { throw std::runtime_error("package: spec not found in graph"); }

  eng.ensure_pkg_at_phase(p->key, pkg_phase::completion);

  if (p->type != pkg_type::CACHE_MANAGED) {
    throw std::runtime_error("package: package is not cache-managed");
  }

  tui::print_stdout("%s\n", p->pkg_path.string().c_str());
}

}  // namespace envy
