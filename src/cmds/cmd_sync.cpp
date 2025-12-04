#include "cmd_sync.h"

#include "cache.h"
#include "engine.h"
#include "manifest.h"
#include "platform.h"
#include "recipe_spec.h"
#include "tui.h"

#include <stdexcept>

namespace envy {

cmd_sync::cmd_sync(cfg cfg) : cfg_{ std::move(cfg) } {}

bool cmd_sync::execute() {
  try {
    auto const m{ manifest::load(manifest::find_manifest_path(cfg_.manifest_path)) };
    if (!m) { throw std::runtime_error("could not load manifest"); }

    // Build set of targets to sync (pointers to specs in manifest)
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
          tui::error("identity '%s' not found in manifest", identity.c_str());
          return false;
        }
      }
    }

    if (targets.empty()) {
      tui::info("nothing to sync");
      return true;
    }

    auto const cache_root{ [&]() -> std::filesystem::path {
      if (cfg_.cache_root) {
        return *cfg_.cache_root;
      } else {
        auto default_cache_root{ platform::get_default_cache_root() };
        if (!default_cache_root) {
          throw std::runtime_error("could not determine cache root");
        }
        return *default_cache_root;
      }
    }() };

    cache c{ cache_root };
    engine eng{ c, m->get_default_shell(nullptr) };
    auto result{ eng.run_full(targets) };

    size_t completed{ 0 };
    size_t user_managed{ 0 };
    size_t failed{ 0 };

    for (auto const &[key, outcome] : result) {
      if (outcome.type == recipe_type::UNKNOWN) {
        failed++;
      } else if (outcome.type == recipe_type::USER_MANAGED) {
        user_managed++;
      } else {
        completed++;
      }
    }

    tui::info("sync complete: %zu package(s), %zu user-managed, %zu failed",
              completed,
              user_managed,
              failed);
    return failed == 0;

  } catch (std::exception const &ex) {
    tui::error("sync command failed: %s", ex.what());
    return false;
  }
}

}  // namespace envy
