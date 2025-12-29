#include "cmd_engine_functional_test.h"

#include "cache.h"
#include "engine.h"
#include "manifest.h"
#include "recipe_spec.h"
#include "test_support.h"
#include "tui.h"

#include "CLI11.hpp"

namespace envy {

void cmd_engine_functional_test::register_cli(CLI::App &app,
                                              std::function<void(cfg)> on_selected) {
  auto *sub{ app.add_subcommand("engine-test", "Test engine execution") };
  auto *cfg_ptr{ new cfg{} };
  sub->add_option("identity", cfg_ptr->identity, "Recipe identity")->required();
  sub->add_option("recipe_path", cfg_ptr->recipe_path, "Recipe file path")
      ->required()
      ->check(CLI::ExistingFile);
  sub->add_option("--fail-after-fetch-count",
                  cfg_ptr->fail_after_fetch_count,
                  "Fail after N successful file downloads (test only)")
      ->default_val(-1);
  sub->callback(
      [cfg_ptr, on_selected = std::move(on_selected)] { on_selected(*cfg_ptr); });
}

cmd_engine_functional_test::cmd_engine_functional_test(
    cfg cfg,
    std::optional<std::filesystem::path> const &cli_cache_root)
    : cfg_{ std::move(cfg) }, cli_cache_root_{ cli_cache_root } {}

void cmd_engine_functional_test::execute() {
  auto c{ cache::ensure(cli_cache_root_, std::nullopt) };

  // Set up test fail counter
  if (cfg_.fail_after_fetch_count > 0) {
    test::set_fail_after_fetch_count(cfg_.fail_after_fetch_count);
  }

  // Build recipe
  recipe_spec *recipe_cfg{ recipe_spec::pool()->emplace(
      cfg_.identity,
      recipe_spec::local_source{ .file_path = cfg_.recipe_path },
      "{}",
      std::nullopt,
      nullptr,
      nullptr,
      std::vector<recipe_spec *>{},
      std::nullopt,
      std::filesystem::path{}) };

  // Create minimal manifest for engine (no DEFAULT_SHELL for tests)
  auto m{ manifest::load("PACKAGES = {}", cfg_.recipe_path) };

  // Run engine
  engine eng{ *c, m->get_default_shell(nullptr) };
  auto result{ eng.run_full({ recipe_cfg }) };

  // Output results as key -> type (avoid = which appears in option keys)
  for (auto const &[id, res] : result) {
    auto const type_str{ [&]() {
      switch (res.type) {
        case recipe_type::CACHE_MANAGED: return "cache-managed";
        case recipe_type::USER_MANAGED: return "user-managed";
        case recipe_type::UNKNOWN: return "unknown";
      }
      return "unknown";
    }() };
    tui::print_stdout("%s -> %s\n", id.c_str(), type_str);
  }
}

}  // namespace envy
