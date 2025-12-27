#include "product_util.h"

#include "engine.h"
#include "recipe.h"
#include "recipe_key.h"
#include "recipe_spec.h"

#include "doctest.h"

#include <filesystem>
#include <memory>

namespace envy {

namespace {

std::unique_ptr<recipe> make_recipe(std::string identity, recipe_type type) {
  recipe_spec *spec{ recipe_spec::pool()->emplace(std::move(identity),
                                                  recipe_spec::weak_ref{},
                                                  "{}",
                                                  std::nullopt,
                                                  nullptr,
                                                  nullptr,
                                                  std::vector<recipe_spec *>{},
                                                  std::nullopt,
                                                  std::filesystem::path{}) };

  return std::unique_ptr<recipe>(new recipe{
      .key = recipe_key(*spec),
      .spec = spec,
      .cache_ptr = nullptr,
      .default_shell_ptr = nullptr,
      .tui_section = {},
      .exec_ctx = nullptr,
      .lua = nullptr,
      .lock = nullptr,
      .canonical_identity_hash = {},
      .asset_path = {},
      .recipe_file_path = std::nullopt,
      .result_hash = {},
      .type = type,
  });
}

}  // namespace

TEST_CASE("product_util_resolve returns joined path for cache-managed provider") {
  auto provider{ make_recipe("local.provider@v1", recipe_type::CACHE_MANAGED) };
  provider->asset_path = std::filesystem::path("/tmp/provider");
  provider->products["tool"] = "bin/tool";

  auto const value{ product_util_resolve(provider.get(), "tool") };
  CHECK(value == "/tmp/provider/bin/tool");
}

TEST_CASE("product_util_resolve returns raw value for user-managed provider") {
  auto provider{ make_recipe("local.provider@v1", recipe_type::USER_MANAGED) };
  provider->products["tool"] = "raw-tool";

  auto const value{ product_util_resolve(provider.get(), "tool") };
  CHECK(value == "raw-tool");
}

TEST_CASE("product_util_resolve throws on missing provider") {
  CHECK_THROWS_WITH_AS(product_util_resolve(nullptr, "tool"),
                       "Product 'tool' has no provider",
                       std::runtime_error);
}

TEST_CASE("product_util_resolve throws on missing product entry") {
  auto provider{ make_recipe("local.provider@v1", recipe_type::CACHE_MANAGED) };
  CHECK_THROWS_WITH_AS(product_util_resolve(provider.get(), "tool"),
                       "Product 'tool' not found in provider 'local.provider@v1'",
                       std::runtime_error);
}

TEST_CASE("product_util_resolve throws on empty product value") {
  auto provider{ make_recipe("local.provider@v1", recipe_type::CACHE_MANAGED) };
  provider->products["tool"] = "";
  CHECK_THROWS_WITH_AS(product_util_resolve(provider.get(), "tool"),
                       "Product 'tool' is empty in provider 'local.provider@v1'",
                       std::runtime_error);
}

TEST_CASE("product_util_resolve throws on missing asset path for cached provider") {
  auto provider{ make_recipe("local.provider@v1", recipe_type::CACHE_MANAGED) };
  provider->products["tool"] = "bin/tool";
  CHECK_THROWS_WITH_AS(product_util_resolve(provider.get(), "tool"),
                       "Product 'tool' provider 'local.provider@v1' missing asset path",
                       std::runtime_error);
}

}  // namespace envy
