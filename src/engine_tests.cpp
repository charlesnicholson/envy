#include "engine.h"

#include "cache.h"
#include "doctest.h"
#include "manifest.h"

#include <filesystem>
#include <vector>

namespace envy {

TEST_CASE("engine_validate_dependency_cycle: no cycle") {
  std::vector<std::string> const ancestors{ "A", "B", "C" };
  CHECK_NOTHROW(engine_validate_dependency_cycle("D", ancestors, "C", "Dependency"));
}

TEST_CASE("engine_validate_dependency_cycle: direct self-loop") {
  std::vector<std::string> const ancestors{ "A", "B" };
  CHECK_THROWS_WITH(engine_validate_dependency_cycle("C", ancestors, "C", "Dependency"),
                    "Dependency cycle detected: C -> C");
}

TEST_CASE("engine_validate_dependency_cycle: cycle in ancestor chain") {
  std::vector<std::string> const ancestors{ "A", "B", "C" };
  CHECK_THROWS_WITH(engine_validate_dependency_cycle("B", ancestors, "D", "Dependency"),
                    "Dependency cycle detected: B -> C -> D -> B");
}

TEST_CASE("engine_validate_dependency_cycle: cycle at chain start") {
  std::vector<std::string> const ancestors{ "A", "B", "C" };
  CHECK_THROWS_WITH(engine_validate_dependency_cycle("A", ancestors, "D", "Dependency"),
                    "Dependency cycle detected: A -> B -> C -> D -> A");
}

TEST_CASE("engine_validate_dependency_cycle: fetch dependency error message") {
  std::vector<std::string> const ancestors{ "A", "B" };
  CHECK_THROWS_WITH(
      engine_validate_dependency_cycle("A", ancestors, "C", "Fetch dependency"),
      "Fetch dependency cycle detected: A -> B -> C -> A");
}

TEST_CASE("engine_validate_dependency_cycle: empty ancestor chain with self-loop") {
  std::vector<std::string> const ancestors{};
  CHECK_THROWS_WITH(engine_validate_dependency_cycle("A", ancestors, "A", "Dependency"),
                    "Dependency cycle detected: A -> A");
}

TEST_CASE("engine_validate_dependency_cycle: empty ancestor chain without cycle") {
  std::vector<std::string> const ancestors{};
  CHECK_NOTHROW(engine_validate_dependency_cycle("B", ancestors, "A", "Dependency"));
}

TEST_CASE("engine_extend_dependencies: extends full closure") {
  // Setup: gn -> ninja -> python (chain), plus unrelated uv
  namespace fs = std::filesystem;
  fs::path const cache_root{ fs::temp_directory_path() / "envy-extend-deps-test-1" };

  cache c{ cache_root };
  auto m{ manifest::load("PACKAGES = {}",
                         fs::path("test_data/recipes/dependency_chain_gn.lua")) };
  engine eng{ c, m->get_default_shell(nullptr) };

  // Create specs for gn, ninja, python, uv
  std::vector<recipe_spec const *> roots;
  recipe_spec *gn_spec = recipe_spec::pool()->emplace(
      "local.gn@r0",
      recipe_spec::local_source{
          .file_path = fs::path("test_data/recipes/dependency_chain_gn.lua") },
      "{}",
      std::nullopt,
      nullptr,
      nullptr,
      std::vector<recipe_spec *>{},
      std::nullopt,
      fs::path{});
  recipe_spec *uv_spec = recipe_spec::pool()->emplace(
      "local.uv@r0",
      recipe_spec::local_source{ .file_path =
                                     fs::path("test_data/recipes/simple_uv.lua") },
      "{}",
      std::nullopt,
      nullptr,
      nullptr,
      std::vector<recipe_spec *>{},
      std::nullopt,
      fs::path{});
  roots.push_back(gn_spec);
  roots.push_back(uv_spec);

  // resolve_graph starts all at recipe_fetch
  eng.resolve_graph(roots);

  // All should be at recipe_fetch after resolve
  CHECK(eng.get_recipe_target_phase(recipe_key(*gn_spec)) == recipe_phase::recipe_fetch);
  CHECK(eng.get_recipe_target_phase(recipe_key(*uv_spec)) == recipe_phase::recipe_fetch);

  // Find gn recipe
  recipe *gn_recipe = eng.find_exact(recipe_key(*gn_spec));
  REQUIRE(gn_recipe != nullptr);

  // Extend gn's closure
  eng.extend_dependencies_to_completion(gn_recipe);

  // gn and its dependencies should be at completion
  CHECK(eng.get_recipe_target_phase(recipe_key(*gn_spec)) == recipe_phase::completion);
  // ninja and python should also be extended (they're gn's dependencies)

  // uv should still be at recipe_fetch (not in gn's closure)
  CHECK(eng.get_recipe_target_phase(recipe_key(*uv_spec)) == recipe_phase::recipe_fetch);

  fs::remove_all(cache_root);
}

TEST_CASE("engine_extend_dependencies: leaf recipe only extends itself") {
  namespace fs = std::filesystem;
  fs::path const cache_root{ fs::temp_directory_path() / "envy-extend-deps-test-2" };

  cache c{ cache_root };
  auto m{ manifest::load("PACKAGES = {}",
                         fs::path("test_data/recipes/dependency_chain_gn.lua")) };
  engine eng{ c, m->get_default_shell(nullptr) };

  std::vector<recipe_spec const *> roots;
  recipe_spec *gn_spec = recipe_spec::pool()->emplace(
      "local.gn@r0",
      recipe_spec::local_source{
          .file_path = fs::path("test_data/recipes/dependency_chain_gn.lua") },
      "{}",
      std::nullopt,
      nullptr,
      nullptr,
      std::vector<recipe_spec *>{},
      std::nullopt,
      fs::path{});
  recipe_spec *python_spec = recipe_spec::pool()->emplace(
      "local.python@r0",
      recipe_spec::local_source{ .file_path =
                                     fs::path("test_data/recipes/simple_python.lua") },
      "{}",
      std::nullopt,
      nullptr,
      nullptr,
      std::vector<recipe_spec *>{},
      std::nullopt,
      fs::path{});
  roots.push_back(gn_spec);
  roots.push_back(python_spec);

  eng.resolve_graph(roots);

  // Find python (leaf with no dependencies)
  recipe *python_recipe = eng.find_exact(recipe_key(*python_spec));
  REQUIRE(python_recipe != nullptr);

  // Extend python
  eng.extend_dependencies_to_completion(python_recipe);

  // Only python should be at completion
  CHECK(eng.get_recipe_target_phase(recipe_key(*python_spec)) == recipe_phase::completion);

  // gn should still be at recipe_fetch (not python's dependency)
  CHECK(eng.get_recipe_target_phase(recipe_key(*gn_spec)) == recipe_phase::recipe_fetch);

  fs::remove_all(cache_root);
}

}  // namespace envy
