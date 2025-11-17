#include "cmd_asset.h"

#include "cache.h"
#include "engine.h"
#include "manifest.h"
#include "platform.h"
#include "recipe_spec.h"
#include "tui.h"

#include <sstream>
#include <stdexcept>

namespace envy {

cmd_asset::cmd_asset(cfg cfg) : cfg_{ std::move(cfg) } {}

bool cmd_asset::execute() {
  try {
    auto const m{ manifest::load(manifest::find_manifest_path(cfg_.manifest_path)) };
    if (!m) { throw std::runtime_error("could not load manifest"); }

    std::vector<recipe_spec const *> matches;
    for (auto const &pkg : m->packages) {
      if (pkg.identity == cfg_.identity) { matches.push_back(&pkg); }
    }

    if (matches.empty()) {
      tui::error("not found");
      return false;
    }

    if (matches.size() > 1) {
      std::string first_key{ recipe_spec::format_key(matches[0]->identity,
                                                     matches[0]->options) };
      for (size_t i{ 1 }; i < matches.size(); ++i) {
        std::string key{ recipe_spec::format_key(matches[i]->identity,
                                                 matches[i]->options) };
        if (key != first_key) {
          tui::error("identity '%s' appears multiple times with different options",
                     cfg_.identity.c_str());
          return false;
        }
      }
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
    recipe_spec const &target{ *matches[0] };
    engine eng{ c, m->get_default_shell(nullptr) };
    auto result{ eng.run_full({ target }) };

    auto it{ result.find(recipe_spec::format_key(target.identity, target.options)) };
    if (it == result.end() || it->second.result_hash.empty()) {
      tui::error("not found");
      return false;
    }

    if (it->second.result_hash == "programmatic") {
      tui::error("not found");
      return false;
    }

    tui::print_stdout("%s\n", it->second.asset_path.string().c_str());
    return true;

  } catch (std::exception const &ex) {
    tui::error("asset command failed: %s", ex.what());
    return false;
  }
}

}  // namespace envy
