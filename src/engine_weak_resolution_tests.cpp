#include "engine.h"

#include "cache.h"
#include "doctest.h"
#include "manifest.h"

#include <atomic>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

namespace envy {
namespace {

static recipe_result_map_t run_recipe_from_file(std::string const &identity,
                                                fs::path const &recipe_path) {
  static std::atomic<int> counter{ 0 };
  fs::path const cache_root{ fs::temp_directory_path() /
                             ("envy-weak-unit-" + std::to_string(++counter)) };

  cache c{ cache_root };
  auto manifest_ptr{ manifest::load("PACKAGES = {}", recipe_path) };
  engine eng{ c, manifest_ptr->get_default_shell(nullptr) };

  recipe_spec *spec_ptr{ recipe_spec::pool()->emplace(identity,
                                                      recipe_spec::local_source{
                                                        .file_path = recipe_path
                                                      },
                                                      "{}",
                                                      std::nullopt,
                                                      nullptr,
                                                      nullptr,
                                                      std::vector<recipe_spec *>{},
                                                      std::nullopt,
                                                      std::filesystem::path{}) };

  recipe_result_map_t results{ eng.run_full({ spec_ptr }) };
  fs::remove_all(cache_root);
  return results;
}

static bool contains_recipe(recipe_result_map_t const &results, std::string const &id) {
  return results.find(id) != results.end();
}

}  // namespace

TEST_CASE("weak reference resolves to an existing provider") {
  fs::path const recipe_path{ "test_data/recipes/weak_consumer_ref_only.lua" };
  auto const results{ run_recipe_from_file("local.weak_consumer_ref_only@v1",
                                           recipe_path) };

  CHECK(contains_recipe(results, "local.weak_consumer_ref_only@v1"));
  CHECK(contains_recipe(results, "local.weak_provider@v1"));
}

TEST_CASE("weak dependency uses fallback when no match exists") {
  fs::path const recipe_path{ "test_data/recipes/weak_consumer_fallback.lua" };
  auto const results{ run_recipe_from_file("local.weak_consumer_fallback@v1",
                                           recipe_path) };

  CHECK(contains_recipe(results, "local.weak_consumer_fallback@v1"));
  CHECK(contains_recipe(results, "local.weak_fallback@v1"));
}

TEST_CASE("weak dependency prefers existing match over fallback") {
  fs::path const recipe_path{ "test_data/recipes/weak_consumer_existing.lua" };
  auto const results{ run_recipe_from_file("local.weak_consumer_existing@v1",
                                           recipe_path) };

  CHECK(contains_recipe(results, "local.weak_consumer_existing@v1"));
  CHECK(contains_recipe(results, "local.existing_dep@v1"));
  CHECK(!contains_recipe(results, "local.unused_fallback@v1"));
}

TEST_CASE("ambiguity surfaces an error with both candidates listed") {
  fs::path const recipe_path{ "test_data/recipes/weak_consumer_ambiguous.lua" };
  try {
    run_recipe_from_file("local.weak_consumer_ambiguous@v1", recipe_path);
    CHECK(false);  // Should not reach
  } catch (std::runtime_error const &e) {
    std::string const msg{ e.what() };
    CHECK(msg.find("ambiguous") != std::string::npos);
    CHECK(msg.find("local.dupe@v1") != std::string::npos);
    CHECK(msg.find("local.dupe@v2") != std::string::npos);
  }
}

TEST_CASE("reference-only dependency reports error when graph makes no progress") {
  fs::path const recipe_path{ "test_data/recipes/weak_missing_ref.lua" };
  try {
    run_recipe_from_file("local.weak_missing_ref@v1", recipe_path);
    CHECK(false);
  } catch (std::runtime_error const &e) {
    std::string const msg{ e.what() };
    CHECK(msg.find("local.never_provided") != std::string::npos);
    CHECK(msg.find("no progress") != std::string::npos);
  }
}

TEST_CASE("weak fallbacks resolve across multiple iterations") {
  fs::path const recipe_path{ "test_data/recipes/weak_chain_root.lua" };
  auto const results{ run_recipe_from_file("local.weak_chain_root@v1", recipe_path) };

  CHECK(contains_recipe(results, "local.weak_chain_root@v1"));
  CHECK(contains_recipe(results, "local.chain_b@v1"));
  CHECK(contains_recipe(results, "local.chain_c@v1"));
}

TEST_CASE("reference-only resolution succeeds after fallbacks grow the graph") {
  fs::path const recipe_path{ "test_data/recipes/weak_progress_flat_root.lua" };
  auto const results{ run_recipe_from_file("local.weak_progress_flat_root@v1",
                                           recipe_path) };

  CHECK(contains_recipe(results, "local.weak_progress_flat_root@v1"));
  CHECK(contains_recipe(results, "local.branch_one@v1"));
  CHECK(contains_recipe(results, "local.branch_two@v1"));
  CHECK(contains_recipe(results, "local.shared@v1"));
}

}  // namespace envy
