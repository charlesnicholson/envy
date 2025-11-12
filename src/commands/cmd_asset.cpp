#include "cmd_asset.h"

#include "cache.h"
#include "engine.h"
#include "graph_state.h"
#include "manifest.h"
#include "platform.h"
#include "tui.h"
#include "util.h"

#include <stdexcept>

namespace envy {

cmd_asset::cmd_asset(cfg cfg) : cfg_{ std::move(cfg) } {}

bool cmd_asset::execute() {
  try {
    auto const manifest_path{ [&]() -> std::filesystem::path {  // find manifest
      if (cfg_.manifest_path) {
        auto path{ std::filesystem::absolute(*cfg_.manifest_path) };
        if (!std::filesystem::exists(path)) {
          throw std::runtime_error("manifest not found");
        }
        return path;
      } else {
        auto discovered{ manifest::discover() };
        if (!discovered) { throw std::runtime_error("manifest not found"); }
        return *discovered;
      }
    }() };

    auto const m{ [&]() -> std::unique_ptr<manifest> {  // load manifest
      auto const content{ util_load_file(manifest_path) };
      auto manifest{ manifest::load(reinterpret_cast<char const *>(content.data()),
                                    manifest_path) };
      if (!manifest) { throw std::runtime_error("could not load manifest"); }
      return manifest;
    }() };

    std::vector<recipe_spec const *> matches;
    for (auto const &pkg : m->packages) {
      if (pkg.identity == cfg_.identity) { matches.push_back(&pkg); }
    }

    if (matches.empty()) {
      tui::error("not found");
      return false;
    }

    if (matches.size() > 1) {
      std::string first_key{ make_canonical_key(matches[0]->identity,
                                                matches[0]->options) };
      for (size_t i{ 1 }; i < matches.size(); ++i) {
        std::string key{ make_canonical_key(matches[i]->identity, matches[i]->options) };
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
    auto result{ engine_run({ target }, c, *m) };

    auto it{ result.find(make_canonical_key(target.identity, target.options)) };
    if (it == result.end() || it->second.result_hash.empty()) {
      tui::error("not found");
      return false;
    }

    if (it->second.result_hash == "programmatic") {
      tui::error("not found");
      return false;
    }

    // 6. Success - print asset path
    tui::print_stdout("%s\n", it->second.asset_path.string().c_str());
    return true;

  } catch (std::exception const &ex) {
    tui::error("asset command failed: %s", ex.what());
    return false;
  }
}

}  // namespace envy
