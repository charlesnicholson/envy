#include "cmd_product.h"

#include "cache.h"
#include "cmd_common.h"
#include "engine.h"
#include "manifest.h"
#include "recipe.h"
#include "recipe_spec.h"
#include "tui.h"

#include <filesystem>

namespace envy {

cmd_product::cmd_product(cfg cfg) : cfg_{ std::move(cfg) } {}

bool cmd_product::execute() {
  try {
    auto const m{ load_manifest_or_throw(cfg_.manifest_path) };
    auto const cache_root{ resolve_cache_root(cfg_.cache_root) };

    cache c{ cache_root };
    engine eng{ c, m->get_default_shell(nullptr) };

    std::vector<recipe_spec const *> roots;
    roots.reserve(m->packages.size());
    for (auto *pkg : m->packages) { roots.push_back(pkg); }

    eng.run_full(roots);

    recipe *provider{ eng.find_product_provider(cfg_.product_name) };
    if (!provider) {
      tui::error("Product '%s' has no provider in resolved dependency graph",
                 cfg_.product_name.c_str());
      return false;
    }

    auto const product_it{ provider->products.find(cfg_.product_name) };
    if (product_it == provider->products.end()) {
      tui::error("Product '%s' provider '%s' missing product key (internal error)",
                 cfg_.product_name.c_str(),
                 provider->spec->identity.c_str());
      return false;
    }

    std::string const &value{ product_it->second };
    if (value.empty()) {
      tui::error("product '%s' is empty", cfg_.product_name.c_str());
      return false;
    }

    // Use provider asset_path to construct final output when available
    std::filesystem::path const asset_path{ provider->asset_path };
    bool const programmatic{ provider->result_hash == "programmatic" ||
                             asset_path.empty() };

    if (programmatic) {
      tui::print_stdout("%s\n", value.c_str());
    } else {
      std::filesystem::path full_path{ asset_path / value };
      tui::print_stdout("%s\n", full_path.string().c_str());
    }

    return true;
  } catch (std::exception const &ex) {
    tui::error("product command failed: %s", ex.what());
    return false;
  }
}

}  // namespace envy
