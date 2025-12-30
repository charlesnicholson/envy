#include "cmd_sync.h"

#include "cache.h"
#include "engine.h"
#include "manifest.h"
#include "recipe_spec.h"
#include "tui.h"

#include "CLI11.hpp"

#include <memory>
#include <stdexcept>

namespace envy {

void cmd_sync::register_cli(CLI::App &app, std::function<void(cfg)> on_selected) {
  auto *sub{ app.add_subcommand("sync", "Install packages from manifest") };
  auto cfg_ptr{ std::make_shared<cfg>() };
  sub->add_option("identities",
                  cfg_ptr->identities,
                  "Recipe identities to sync (sync all if omitted)");
  sub->add_option("--manifest", cfg_ptr->manifest_path, "Path to envy.lua manifest");
  sub->callback(
      [cfg_ptr, on_selected = std::move(on_selected)] { on_selected(*cfg_ptr); });
}

cmd_sync::cmd_sync(cfg cfg, std::optional<std::filesystem::path> const &cli_cache_root)
    : cfg_{ std::move(cfg) }, cli_cache_root_{ cli_cache_root } {}

void cmd_sync::execute() {
  auto const m{ manifest::load(manifest::find_manifest_path(cfg_.manifest_path)) };
  if (!m) { throw std::runtime_error("sync: could not load manifest"); }

  auto c{ cache::ensure(cli_cache_root_, m->meta.cache) };

  std::vector<recipe_spec const *> targets;

  if (cfg_.identities.empty()) {  // Sync entire manifest
    for (auto const *pkg : m->packages) { targets.push_back(pkg); }
  } else {  // Sync specific identities - validate all exist first
    for (auto const &identity : cfg_.identities) {
      bool found{ false };
      for (auto const *pkg : m->packages) {
        if (pkg->identity == identity) {
          targets.push_back(pkg);
          found = true;
          break;  // Take first match for this identity
        }
      }
      if (!found) {
        throw std::runtime_error("sync: identity '" + identity +
                                 "' not found in manifest");
      }
    }
  }

  if (targets.empty()) {
    tui::info("nothing to sync");
    return;
  }

  engine eng{ *c, m->get_default_shell(nullptr) };
  auto result{ eng.run_full(targets) };

  size_t completed{ 0 };
  size_t user_managed{ 0 };
  size_t failed{ 0 };

  for (auto const &[key, outcome] : result) {
    if (outcome.type == recipe_type::UNKNOWN) {
      ++failed;
    } else if (outcome.type == recipe_type::USER_MANAGED) {
      ++user_managed;
    } else {
      ++completed;
    }
  }

  tui::info("sync complete: %zu package(s), %zu user-managed, %zu failed",
            completed,
            user_managed,
            failed);

  if (failed > 0) {
    throw std::runtime_error("sync: " + std::to_string(failed) + " failed");
  }
}

}  // namespace envy
