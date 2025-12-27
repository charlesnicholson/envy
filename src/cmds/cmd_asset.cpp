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

cmd_asset::cmd_asset(cfg cfg, cache &c) : cfg_{ std::move(cfg) }, cache_{ c } {}

void cmd_asset::execute() {
  auto const m{ load_manifest_or_throw(cfg_.manifest_path) };

  std::vector<recipe_spec const *> matches;
  for (auto const *pkg : m->packages) {
    if (pkg->identity == cfg_.identity) { matches.push_back(pkg); }
  }

  if (matches.empty()) { throw std::runtime_error("asset: identity not found"); }

  if (matches.size() > 1) {
    std::string first_key{ recipe_spec::format_key(matches[0]->identity,
                                                   matches[0]->serialized_options) };
    for (size_t i{ 1 }; i < matches.size(); ++i) {
      std::string key{ recipe_spec::format_key(matches[i]->identity,
                                               matches[i]->serialized_options) };
      if (key != first_key) {
        throw std::runtime_error("asset: identity '" + cfg_.identity +
                                 "' appears multiple times with different options");
      }
    }
  }

  engine eng{ cache_, m->get_default_shell(nullptr) };

  std::vector<recipe_spec const *> roots;
  roots.reserve(m->packages.size());
  for (auto *pkg : m->packages) { roots.push_back(pkg); }

  eng.resolve_graph(roots);

  recipe_key const target_key{ *matches[0] };
  recipe *r{ eng.find_exact(target_key) };
  if (!r) { throw std::runtime_error("asset: recipe not found in graph"); }

  eng.ensure_recipe_at_phase(r->key, recipe_phase::completion);

  if (r->type != recipe_type::CACHE_MANAGED) {
    throw std::runtime_error("asset: recipe is not cache-managed");
  }

  tui::print_stdout("%s\n", r->asset_path.string().c_str());
}

}  // namespace envy
