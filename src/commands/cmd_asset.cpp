#include "cmd_asset.h"

#include "cache.h"
#include "engine.h"
#include "graph_state.h"
#include "manifest.h"
#include "platform.h"
#include "tui.h"

#include <fstream>
#include <stdexcept>

namespace envy {

cmd_asset::cmd_asset(cfg cfg) : cfg_{ std::move(cfg) } {}

bool cmd_asset::execute() {
  try {
    // 1. Discover or load manifest
    std::filesystem::path manifest_path;
    if (cfg_.manifest_path) {
      manifest_path = std::filesystem::absolute(*cfg_.manifest_path);
      if (!std::filesystem::exists(manifest_path)) {
        tui::error("not found");
        return false;
      }
    } else {
      auto discovered{ manifest::discover() };
      if (!discovered) {
        tui::error("not found");
        return false;
      }
      manifest_path = *discovered;
    }

    // Read manifest file
    std::ifstream file{ manifest_path };
    if (!file) {
      tui::error("not found");
      return false;
    }
    std::string content{ (std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>() };

    auto m{ manifest::load(content.c_str(), manifest_path) };
    if (!m) {
      tui::error("not found");
      return false;
    }

    // 2. Find matching package(s) and detect ambiguity
    std::vector<recipe_spec const *> matches;
    for (auto const &pkg : m->packages) {
      if (pkg.identity == cfg_.identity) { matches.push_back(&pkg); }
    }

    if (matches.empty()) {
      tui::error("not found");
      return false;
    }

    // Check for ambiguity (same identity with different options)
    // Note: We check if all matches have identical canonical keys to detect different
    // options
    if (matches.size() > 1) {
      std::string first_key{ make_canonical_key(matches[0]->identity,
                                                matches[0]->options) };
      for (size_t i = 1; i < matches.size(); ++i) {
        std::string key{ make_canonical_key(matches[i]->identity, matches[i]->options) };
        if (key != first_key) {
          tui::error("identity '%s' appears multiple times with different options",
                     cfg_.identity.c_str());
          return false;
        }
      }
    }

    recipe_spec const &target{ *matches[0] };

    // 3. Run engine with ONLY this package as root
    auto const cache_root{ cfg_.cache_root ? *cfg_.cache_root
                                           : platform::get_default_cache_root().value() };

    cache c{ cache_root };
    auto result{ engine_run({ target }, c, *m) };

    // 4. Look up result using canonical key
    std::string canonical_key{ make_canonical_key(target.identity, target.options) };

    auto it{ result.find(canonical_key) };
    if (it == result.end() || it->second.result_hash.empty()) {
      tui::error("not found");
      return false;
    }

    if (it->second.result_hash == "programmatic") {
      tui::error("not found");
      return false;
    }

    // 5. Success - print asset path
    tui::print_stdout("%s\n", it->second.asset_path.string().c_str());
    return true;

  } catch (std::exception const &ex) {
    tui::error("not found");
    return false;
  }
}

}  // namespace envy
