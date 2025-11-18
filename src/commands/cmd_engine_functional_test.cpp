#include "cmd_engine_functional_test.h"

#include "cache.h"
#include "engine.h"
#include "manifest.h"
#include "platform.h"
#include "recipe_spec.h"
#include "test_support.h"
#include "tui.h"

namespace envy {

cmd_engine_functional_test::cmd_engine_functional_test(cfg cfg) : cfg_{ std::move(cfg) } {}

bool cmd_engine_functional_test::execute() {
  // Set up test fail counter
  if (cfg_.fail_after_fetch_count > 0) {
    test::set_fail_after_fetch_count(cfg_.fail_after_fetch_count);
  }

  // Get cache root
  auto const cache_root{ cfg_.cache_root ? *cfg_.cache_root
                                         : platform::get_default_cache_root().value() };

  cache c{ cache_root };

  // Build recipe
  recipe_spec recipe_cfg{ .identity = cfg_.identity,
                          .source =
                              recipe_spec::local_source{ .file_path = cfg_.recipe_path },
                          .options = {},
                          .needed_by = std::nullopt };

  // Create minimal manifest for engine (no default_shell for tests)
  auto m{ manifest::load("packages = {}", cfg_.recipe_path) };

  // Run engine
  engine eng{ c, m->get_default_shell(nullptr) };
  auto result{ eng.run_full({ recipe_cfg }) };

  // Output results as key -> value lines (avoid = which appears in option keys)
  for (auto const &[id, res] : result) {
    tui::print_stdout("%s -> %s\n", id.c_str(), res.result_hash.c_str());
  }

  return true;
}

}  // namespace envy
