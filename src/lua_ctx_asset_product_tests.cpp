#include "lua_ctx/lua_ctx_bindings.h"

#include "engine.h"
#include "product_util.h"
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

  return std::unique_ptr<recipe>(new recipe{ .key = recipe_key(*spec),
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
                                             .type = type });
}

}  // namespace

// =====================
// ctx.asset() tests
// =====================

TEST_CASE("ctx.asset succeeds when dependency reachable and ready") {
  auto dep{ make_recipe("local.dep@v1", recipe_type::CACHE_MANAGED) };
  dep->asset_path = std::filesystem::path("/tmp/dep");

  auto consumer{ make_recipe("local.consumer@v1", recipe_type::CACHE_MANAGED) };
  recipe_execution_ctx exec{ .current_phase = recipe_phase::asset_install };
  consumer->exec_ctx = &exec;
  consumer->dependencies["local.dep@v1"] = { dep.get(), recipe_phase::asset_stage };

  lua_ctx_common ctx{ .fetch_dir = {},
                      .run_dir = {},
                      .engine_ = nullptr,
                      .recipe_ = consumer.get() };
  auto asset_fn{ make_ctx_asset(&ctx) };

  CHECK(asset_fn("local.dep@v1") == "/tmp/dep");
}

TEST_CASE("ctx.asset rejects access before needed_by phase") {
  auto dep{ make_recipe("local.dep@v1", recipe_type::CACHE_MANAGED) };
  dep->asset_path = std::filesystem::path("/tmp/dep");

  auto consumer{ make_recipe("local.consumer@v1", recipe_type::CACHE_MANAGED) };
  recipe_execution_ctx exec{ .current_phase = recipe_phase::asset_fetch };
  consumer->exec_ctx = &exec;
  consumer->dependencies["local.dep@v1"] = { dep.get(), recipe_phase::asset_stage };

  lua_ctx_common ctx{ .fetch_dir = {},
                      .run_dir = {},
                      .engine_ = nullptr,
                      .recipe_ = consumer.get() };
  auto asset_fn{ make_ctx_asset(&ctx) };

  CHECK_THROWS_WITH(asset_fn("local.dep@v1"),
                    "ctx.asset: dependency 'local.dep@v1' needed_by 'stage' but accessed "
                    "during 'fetch'");
}

TEST_CASE("ctx.asset rejects user-managed dependencies") {
  auto dep{ make_recipe("local.dep@v1", recipe_type::USER_MANAGED) };

  auto consumer{ make_recipe("local.consumer@v1", recipe_type::CACHE_MANAGED) };
  recipe_execution_ctx exec{ .current_phase = recipe_phase::asset_stage };
  consumer->exec_ctx = &exec;
  consumer->dependencies["local.dep@v1"] = { dep.get(), recipe_phase::asset_stage };

  lua_ctx_common ctx{ .fetch_dir = {},
                      .run_dir = {},
                      .engine_ = nullptr,
                      .recipe_ = consumer.get() };
  auto asset_fn{ make_ctx_asset(&ctx) };

  CHECK_THROWS_WITH(
      asset_fn("local.dep@v1"),
      "ctx.asset: dependency 'local.dep@v1' is user-managed and has no asset path");
}

TEST_CASE("ctx.asset rejects when no strong dependency path exists") {
  auto consumer{ make_recipe("local.consumer@v1", recipe_type::CACHE_MANAGED) };
  recipe_execution_ctx exec{ .current_phase = recipe_phase::asset_stage };
  consumer->exec_ctx = &exec;

  lua_ctx_common ctx{ .fetch_dir = {},
                      .run_dir = {},
                      .engine_ = nullptr,
                      .recipe_ = consumer.get() };
  auto asset_fn{ make_ctx_asset(&ctx) };

  CHECK_THROWS_WITH(asset_fn("local.missing@v1"),
                    "ctx.asset: recipe 'local.consumer@v1' has no strong dependency on "
                    "'local.missing@v1'");
}

TEST_CASE("ctx.asset picks earliest needed_by among multiple strong paths") {
  auto target{ make_recipe("local.target@v1", recipe_type::CACHE_MANAGED) };
  target->asset_path = std::filesystem::path("/tmp/target");

  auto mid{ make_recipe("local.mid@v1", recipe_type::CACHE_MANAGED) };
  mid->dependencies["local.target@v1"] = { target.get(), recipe_phase::asset_build };

  auto consumer{ make_recipe("local.consumer@v1", recipe_type::CACHE_MANAGED) };
  recipe_execution_ctx exec{ .current_phase = recipe_phase::asset_stage };
  consumer->exec_ctx = &exec;
  consumer->dependencies["local.target@v1"] = { target.get(),
                                                recipe_phase::asset_install };
  consumer->dependencies["local.mid@v1"] = { mid.get(), recipe_phase::asset_fetch };

  lua_ctx_common ctx{ .fetch_dir = {},
                      .run_dir = {},
                      .engine_ = nullptr,
                      .recipe_ = consumer.get() };
  auto asset_fn{ make_ctx_asset(&ctx) };

  // Even though the direct edge needs target by install, the mid edge needs it by fetch,
  // so access during stage should succeed.
  CHECK(asset_fn("local.target@v1") == "/tmp/target");
}

// =====================
// ctx.product() tests
// =====================

TEST_CASE("ctx.product returns provider product when phase satisfied") {
  auto provider{ make_recipe("local.provider@v1", recipe_type::CACHE_MANAGED) };
  provider->asset_path = std::filesystem::path("/tmp/prov");
  provider->products["tool"] = "bin/tool";

  auto consumer{ make_recipe("local.consumer@v1", recipe_type::CACHE_MANAGED) };
  recipe_execution_ctx exec{ .current_phase = recipe_phase::asset_build };
  consumer->exec_ctx = &exec;
  consumer->product_dependencies["tool"] = recipe::product_dependency{
    .name = "tool",
    .needed_by = recipe_phase::asset_stage,
    .provider = provider.get(),
    .constraint_identity = provider->spec->identity,
  };

  lua_ctx_common ctx{ .fetch_dir = {},
                      .run_dir = {},
                      .engine_ = nullptr,
                      .recipe_ = consumer.get() };
  auto product_fn{ make_ctx_product(&ctx) };

  CHECK(product_fn("tool") == "/tmp/prov/bin/tool");
}

TEST_CASE("ctx.product rejects access before needed_by") {
  auto provider{ make_recipe("local.provider@v1", recipe_type::CACHE_MANAGED) };
  provider->asset_path = std::filesystem::path("/tmp/prov");
  provider->products["tool"] = "bin/tool";

  auto consumer{ make_recipe("local.consumer@v1", recipe_type::CACHE_MANAGED) };
  recipe_execution_ctx exec{ .current_phase = recipe_phase::asset_fetch };
  consumer->exec_ctx = &exec;
  consumer->product_dependencies["tool"] = recipe::product_dependency{
    .name = "tool",
    .needed_by = recipe_phase::asset_install,
    .provider = provider.get(),
    .constraint_identity = provider->spec->identity,
  };

  lua_ctx_common ctx{ .fetch_dir = {},
                      .run_dir = {},
                      .engine_ = nullptr,
                      .recipe_ = consumer.get() };
  auto product_fn{ make_ctx_product(&ctx) };

  CHECK_THROWS_WITH(
      product_fn("tool"),
      "ctx.product: product 'tool' needed_by 'install' but accessed during 'fetch'");
}

TEST_CASE("ctx.product rejects missing declaration") {
  auto consumer{ make_recipe("local.consumer@v1", recipe_type::CACHE_MANAGED) };
  recipe_execution_ctx exec{ .current_phase = recipe_phase::asset_stage };
  consumer->exec_ctx = &exec;

  lua_ctx_common ctx{ .fetch_dir = {},
                      .run_dir = {},
                      .engine_ = nullptr,
                      .recipe_ = consumer.get() };
  auto product_fn{ make_ctx_product(&ctx) };

  CHECK_THROWS_WITH(
      product_fn("tool"),
      "ctx.product: recipe 'local.consumer@v1' does not declare product dependency on "
      "'tool'");
}

TEST_CASE("ctx.product rejects constraint mismatch") {
  auto provider{ make_recipe("local.provider@v1", recipe_type::CACHE_MANAGED) };
  provider->asset_path = std::filesystem::path("/tmp/prov");
  provider->products["tool"] = "bin/tool";

  auto consumer{ make_recipe("local.consumer@v1", recipe_type::CACHE_MANAGED) };
  recipe_execution_ctx exec{ .current_phase = recipe_phase::asset_build };
  consumer->exec_ctx = &exec;
  consumer->product_dependencies["tool"] =
      recipe::product_dependency{ .name = "tool",
                                  .needed_by = recipe_phase::asset_stage,
                                  .provider = provider.get(),
                                  .constraint_identity = "local.other@v1" };

  lua_ctx_common ctx{ .fetch_dir = {},
                      .run_dir = {},
                      .engine_ = nullptr,
                      .recipe_ = consumer.get() };
  auto product_fn{ make_ctx_product(&ctx) };

  CHECK_THROWS_WITH(
      product_fn("tool"),
      "ctx.product: product 'tool' must come from 'local.other@v1', but provider is "
      "'local.provider@v1'");
}

TEST_CASE("ctx.product rejects unresolved provider") {
  auto consumer{ make_recipe("local.consumer@v1", recipe_type::CACHE_MANAGED) };
  recipe_execution_ctx exec{ .current_phase = recipe_phase::asset_stage };
  consumer->exec_ctx = &exec;
  consumer->product_dependencies["tool"] =
      recipe::product_dependency{ .name = "tool",
                                  .needed_by = recipe_phase::asset_stage,
                                  .provider = nullptr,
                                  .constraint_identity = "" };

  lua_ctx_common ctx{ .fetch_dir = {},
                      .run_dir = {},
                      .engine_ = nullptr,
                      .recipe_ = consumer.get() };
  auto product_fn{ make_ctx_product(&ctx) };

  CHECK_THROWS_WITH(product_fn("tool"),
                    "ctx.product: product 'tool' provider not resolved for recipe "
                    "'local.consumer@v1'");
}

}  // namespace envy
