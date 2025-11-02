#include "cmd_engine_functional_test.h"

#include "cache.h"
#include "engine.h"
#include "platform.h"
#include "recipe.h"
#include "tui.h"

#include <cstdio>

namespace envy {

cmd_engine_functional_test::cmd_engine_functional_test(cfg cfg) : cfg_{ std::move(cfg) } {}

bool cmd_engine_functional_test::execute() {
  // Get cache root
  auto const cache_root{ cfg_.cache_root ? *cfg_.cache_root
                                         : platform::get_default_cache_root().value() };

  cache c{ cache_root };

  // Build recipe::cfg
  recipe::cfg recipe_cfg{ .identity = cfg_.identity,
                          .source =
                              recipe::cfg::local_source{ .file_path = cfg_.recipe_path },
                          .options = {},
                          .needed_by = std::nullopt };

  // Run engine
  auto result{ engine_run({ recipe_cfg }, c) };

  // Output results as key=value lines
  for (auto const &[id, hash] : result) {
    tui::print_stdout("%s=%s\n", id.c_str(), hash.c_str());
  }

  return true;
}

}  // namespace envy
