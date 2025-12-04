#include "phase_recipe_fetch.h"

#include "cache.h"
#include "engine.h"
#include "recipe.h"
#include "recipe_spec.h"

#include "doctest.h"

#include <filesystem>

namespace envy {

namespace {

recipe_spec *make_local_spec(std::string identity,
                             std::filesystem::path const &file_path) {
  return recipe_spec::pool()->emplace(std::move(identity),
                                      recipe_spec::local_source{ file_path },
                                      "{}",
                                      std::nullopt,
                                      nullptr,
                                      nullptr,
                                      std::vector<recipe_spec *>{},
                                      std::nullopt,
                                      file_path);
}

std::filesystem::path repo_path(std::string const &rel) {
  return std::filesystem::current_path() / rel;
}

}  // namespace

TEST_CASE("function check/install classified as user-managed") {
  cache c;
  engine eng{ c, std::nullopt };

  auto *spec{ make_local_spec("local.brew@r0", repo_path("examples/local.brew@r0.lua")) };
  recipe *r{ eng.ensure_recipe(spec) };

  REQUIRE_NOTHROW(run_recipe_fetch_phase(r, eng));
  CHECK(r->type == recipe_type::USER_MANAGED);
}

TEST_CASE("string check/install classified as user-managed") {
  cache c;
  engine eng{ c, std::nullopt };

  auto *spec{ make_local_spec("local.string_check_install@v1",
                              repo_path("test_data/recipes/string_check_install.lua")) };
  recipe *r{ eng.ensure_recipe(spec) };

  REQUIRE_NOTHROW(run_recipe_fetch_phase(r, eng));
  CHECK(r->type == recipe_type::USER_MANAGED);
}

TEST_CASE("string check without install errors") {
  cache c;
  engine eng{ c, std::nullopt };

  auto *spec{ make_local_spec("local.string_check_only@v1",
                              repo_path("test_data/recipes/string_check_only.lua")) };
  recipe *r{ eng.ensure_recipe(spec) };

  CHECK_THROWS(run_recipe_fetch_phase(r, eng));
}

}  // namespace envy
