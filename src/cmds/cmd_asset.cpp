#include "cmd_asset.h"

#include "cache.h"
#include "cmd_common.h"
#include "engine.h"
#include "manifest.h"
#include "recipe.h"
#include "recipe_key.h"
#include "recipe_spec.h"
#include "tui.h"

namespace envy {

cmd_asset::cmd_asset(cfg cfg) : cfg_{ std::move(cfg) } {}

bool cmd_asset::execute() {
  try {
    auto const m{ load_manifest_or_throw(cfg_.manifest_path) };

    std::vector<recipe_spec const *> matches;
    for (auto const *pkg : m->packages) {
      if (pkg->identity == cfg_.identity) { matches.push_back(pkg); }
    }

    if (matches.empty()) {
      tui::error("not found");
      return false;
    }

    if (matches.size() > 1) {
      std::string first_key{ recipe_spec::format_key(matches[0]->identity,
                                                     matches[0]->serialized_options) };
      for (size_t i{ 1 }; i < matches.size(); ++i) {
        std::string key{ recipe_spec::format_key(matches[i]->identity,
                                                 matches[i]->serialized_options) };
        if (key != first_key) {
          tui::error("identity '%s' appears multiple times with different options",
                     cfg_.identity.c_str());
          return false;
        }
      }
    }

    auto const cache_root{ resolve_cache_root(cfg_.cache_root) };

    cache c{ cache_root };
    engine eng{ c, m->get_default_shell(nullptr) };

    std::vector<recipe_spec const *> roots;
    roots.reserve(m->packages.size());
    for (auto *pkg : m->packages) { roots.push_back(pkg); }

    eng.resolve_graph(roots);

    recipe_key const target_key{ *matches[0] };
    recipe *r{ eng.find_exact(target_key) };
    if (!r) {
      tui::error("not found");
      return false;
    }

    eng.ensure_recipe_at_phase(r->key, recipe_phase::completion);

    if (r->type != recipe_type::CACHE_MANAGED) {
      tui::error("not found");
      return false;
    }

    tui::print_stdout("%s\n", r->asset_path.string().c_str());
    return true;

  } catch (std::exception const &ex) {
    tui::error("asset command failed: %s", ex.what());
    return false;
  }
}

}  // namespace envy
