#include "engine.h"

#include "cache.h"
#include "doctest.h"
#include "manifest.h"
#include "platform.h"

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
  auto m{ manifest::load("-- @envy bin-dir \"tools\"\nPACKAGES = {}",
                         fs::path("test_data/specs/dependency_chain_gn.lua")) };
  engine eng{ c, m.get() };

  // Create cfgs for gn, ninja, python, uv
  std::vector<pkg_cfg const *> roots;
  pkg_cfg *gn_cfg = pkg_cfg::pool()->emplace(
      "local.gn@r0",
      pkg_cfg::local_source{ .file_path =
                                 fs::path("test_data/specs/dependency_chain_gn.lua") },
      "{}",
      std::nullopt,
      nullptr,
      nullptr,
      std::vector<pkg_cfg *>{},
      std::nullopt,
      fs::path{});
  pkg_cfg *uv_cfg = pkg_cfg::pool()->emplace(
      "local.uv@r0",
      pkg_cfg::local_source{ .file_path = fs::path("test_data/specs/simple_uv.lua") },
      "{}",
      std::nullopt,
      nullptr,
      nullptr,
      std::vector<pkg_cfg *>{},
      std::nullopt,
      fs::path{});
  roots.push_back(gn_cfg);
  roots.push_back(uv_cfg);

  // resolve_graph starts all at spec_fetch
  eng.resolve_graph(roots);

  // All should be at spec_fetch after resolve
  CHECK(eng.get_pkg_target_phase(pkg_key(*gn_cfg)) == pkg_phase::spec_fetch);
  CHECK(eng.get_pkg_target_phase(pkg_key(*uv_cfg)) == pkg_phase::spec_fetch);

  // Find gn package
  pkg *gn_pkg = eng.find_exact(pkg_key(*gn_cfg));
  REQUIRE(gn_pkg != nullptr);

  // Extend gn's closure
  eng.extend_dependencies_to_completion(gn_pkg);

  // gn and its dependencies should be at completion
  CHECK(eng.get_pkg_target_phase(pkg_key(*gn_cfg)) == pkg_phase::completion);
  // ninja and python should also be extended (they're gn's dependencies)

  // uv should still be at spec_fetch (not in gn's closure)
  CHECK(eng.get_pkg_target_phase(pkg_key(*uv_cfg)) == pkg_phase::spec_fetch);

  fs::remove_all(cache_root);
}

TEST_CASE("engine_extend_dependencies: leaf package only extends itself") {
  namespace fs = std::filesystem;
  fs::path const cache_root{ fs::temp_directory_path() / "envy-extend-deps-test-2" };

  cache c{ cache_root };
  auto m{ manifest::load("-- @envy bin-dir \"tools\"\nPACKAGES = {}",
                         fs::path("test_data/specs/dependency_chain_gn.lua")) };
  engine eng{ c, m.get() };

  std::vector<pkg_cfg const *> roots;
  pkg_cfg *gn_cfg = pkg_cfg::pool()->emplace(
      "local.gn@r0",
      pkg_cfg::local_source{ .file_path =
                                 fs::path("test_data/specs/dependency_chain_gn.lua") },
      "{}",
      std::nullopt,
      nullptr,
      nullptr,
      std::vector<pkg_cfg *>{},
      std::nullopt,
      fs::path{});
  pkg_cfg *python_cfg = pkg_cfg::pool()->emplace(
      "local.python@r0",
      pkg_cfg::local_source{ .file_path = fs::path("test_data/specs/simple_python.lua") },
      "{}",
      std::nullopt,
      nullptr,
      nullptr,
      std::vector<pkg_cfg *>{},
      std::nullopt,
      fs::path{});
  roots.push_back(gn_cfg);
  roots.push_back(python_cfg);

  eng.resolve_graph(roots);

  // Find python (leaf with no dependencies)
  pkg *python_pkg = eng.find_exact(pkg_key(*python_cfg));
  REQUIRE(python_pkg != nullptr);

  // Extend python
  eng.extend_dependencies_to_completion(python_pkg);

  // Only python should be at completion
  CHECK(eng.get_pkg_target_phase(pkg_key(*python_cfg)) == pkg_phase::completion);

  // gn should still be at spec_fetch (not python's dependency)
  CHECK(eng.get_pkg_target_phase(pkg_key(*gn_cfg)) == pkg_phase::spec_fetch);

  fs::remove_all(cache_root);
}

TEST_CASE("resolve_graph: spec_fetch failures are propagated") {
  namespace fs = std::filesystem;
  fs::path const cache_root{ fs::temp_directory_path() / "envy-resolve-fail-test" };

  cache c{ cache_root };
  auto m{ manifest::load("-- @envy bin-dir \"tools\"\nPACKAGES = {}",
                         fs::path("test_data/specs/simple_python.lua")) };
  engine eng{ c, m.get() };

  pkg_cfg *bad_cfg = pkg_cfg::pool()->emplace(
      "local.nonexistent@v1",
      pkg_cfg::local_source{ .file_path = fs::path("test_data/specs/DOES_NOT_EXIST.lua") },
      "{}",
      std::nullopt,
      nullptr,
      nullptr,
      std::vector<pkg_cfg *>{},
      std::nullopt,
      fs::path{});

  std::vector<pkg_cfg const *> roots{ bad_cfg };
  CHECK_THROWS_WITH(eng.resolve_graph(roots), doctest::Contains("Spec source not found"));

  fs::remove_all(cache_root);
}

TEST_CASE("process_fetch_dependencies: manifest bundle parent stays null") {
  // When a manifest-declared bundle (with custom fetch) is used as a source_dependency,
  // its parent should remain nullptr because the fetch function is in the manifest,
  // not in the parent spec's Lua state.
  namespace fs = std::filesystem;
  fs::path const cache_root{ fs::temp_directory_path() / "envy-bundle-parent-test" };

  cache c{ cache_root };
  auto m{ manifest::load("-- @envy bin-dir \"tools\"\nPACKAGES = {}",
                         fs::path("test_data/specs/simple_python.lua")) };
  engine eng{ c, m.get() };

  // Create a bundle pkg_cfg like manifest would create it (with parent = nullptr)
  pkg_cfg *bundle_cfg = pkg_cfg::pool()->emplace(
      "test.custom-bundle@v1",  // identity = bundle identity
      pkg_cfg::bundle_source{ .bundle_identity = "test.custom-bundle@v1",
                              .fetch_source = pkg_cfg::custom_fetch_source{} },
      "{}",
      std::nullopt,
      nullptr,  // parent = nullptr (manifest-declared)
      nullptr,
      std::vector<pkg_cfg *>{},
      std::nullopt,
      fs::path{});
  bundle_cfg->bundle_identity = "test.custom-bundle@v1";  // Mark as bundle

  // Create a spec that has the bundle as a source_dependency
  pkg_cfg *spec_cfg = pkg_cfg::pool()->emplace(
      "test.spec@v1",
      pkg_cfg::local_source{ .file_path = fs::path("test_data/specs/simple_python.lua") },
      "{}",
      std::nullopt,
      nullptr,
      nullptr,
      std::vector<pkg_cfg *>{ bundle_cfg },  // bundle as source_dependency
      std::nullopt,
      fs::path{});

  // Verify bundle parent is null before
  CHECK(bundle_cfg->parent == nullptr);

  // Run resolve_graph which will call process_fetch_dependencies
  std::vector<pkg_cfg const *> roots{ spec_cfg };

  // The resolve_graph will fail because the bundle can't actually be fetched,
  // but we can verify the parent wasn't modified before the failure
  try {
    eng.resolve_graph(roots);
  } catch (...) {
    // Expected to fail
  }

  // Verify bundle parent is STILL null (not set to spec_cfg)
  // This is the key assertion - manifest bundles keep nullptr parent
  CHECK(bundle_cfg->parent == nullptr);

  fs::remove_all(cache_root);
}

// --- engine_filter_host_platform ---

TEST_CASE("engine_filter_host_platform: empty platforms passes through") {
  pkg_cfg *cfg = pkg_cfg::pool()->emplace(
      "local.tool@r0",
      pkg_cfg::local_source{ .file_path = std::filesystem::path("dummy.lua") },
      "{}",
      std::nullopt,
      nullptr,
      nullptr,
      std::vector<pkg_cfg *>{},
      std::nullopt,
      std::filesystem::path{});

  auto result{ engine_filter_host_platform({ cfg }) };
  CHECK(result.size() == 1);
  CHECK(result[0] == cfg);
}

TEST_CASE("engine_filter_host_platform: current OS passes through") {
  pkg_cfg *cfg = pkg_cfg::pool()->emplace(
      "local.tool@r0",
      pkg_cfg::local_source{ .file_path = std::filesystem::path("dummy.lua") },
      "{}",
      std::nullopt,
      nullptr,
      nullptr,
      std::vector<pkg_cfg *>{},
      std::nullopt,
      std::filesystem::path{});
  cfg->platforms.push_back(std::string(platform::os_name()));

  auto result{ engine_filter_host_platform({ cfg }) };
  CHECK(result.size() == 1);
}

TEST_CASE("engine_filter_host_platform: other OS is filtered out") {
  std::string const other{ std::string(platform::os_name()) == "darwin" ? "linux"
                                                                        : "darwin" };

  pkg_cfg *cfg = pkg_cfg::pool()->emplace(
      "local.tool@r0",
      pkg_cfg::local_source{ .file_path = std::filesystem::path("dummy.lua") },
      "{}",
      std::nullopt,
      nullptr,
      nullptr,
      std::vector<pkg_cfg *>{},
      std::nullopt,
      std::filesystem::path{});
  cfg->platforms.push_back(other);

  auto result{ engine_filter_host_platform({ cfg }) };
  CHECK(result.empty());
}

TEST_CASE("engine_filter_host_platform: current os-arch passes through") {
  std::string const exact{ std::string(platform::os_name()) + "-" +
                           std::string(platform::arch_name()) };

  pkg_cfg *cfg = pkg_cfg::pool()->emplace(
      "local.tool@r0",
      pkg_cfg::local_source{ .file_path = std::filesystem::path("dummy.lua") },
      "{}",
      std::nullopt,
      nullptr,
      nullptr,
      std::vector<pkg_cfg *>{},
      std::nullopt,
      std::filesystem::path{});
  cfg->platforms.push_back(exact);

  auto result{ engine_filter_host_platform({ cfg }) };
  CHECK(result.size() == 1);
}

TEST_CASE("engine_filter_host_platform: wrong arch is filtered out") {
  std::string const wrong{ std::string(platform::os_name()) + "-" +
                           (std::string(platform::arch_name()) == "arm64" ? "x86_64"
                                                                          : "arm64") };

  pkg_cfg *cfg = pkg_cfg::pool()->emplace(
      "local.tool@r0",
      pkg_cfg::local_source{ .file_path = std::filesystem::path("dummy.lua") },
      "{}",
      std::nullopt,
      nullptr,
      nullptr,
      std::vector<pkg_cfg *>{},
      std::nullopt,
      std::filesystem::path{});
  cfg->platforms.push_back(wrong);

  auto result{ engine_filter_host_platform({ cfg }) };
  CHECK(result.empty());
}

TEST_CASE("engine_filter_host_platform: mixed roots keeps only matching") {
  std::string const other{ std::string(platform::os_name()) == "darwin" ? "linux"
                                                                        : "darwin" };

  pkg_cfg *good = pkg_cfg::pool()->emplace(
      "local.good@r0",
      pkg_cfg::local_source{ .file_path = std::filesystem::path("dummy.lua") },
      "{}",
      std::nullopt,
      nullptr,
      nullptr,
      std::vector<pkg_cfg *>{},
      std::nullopt,
      std::filesystem::path{});
  good->platforms.push_back(std::string(platform::os_name()));

  pkg_cfg *bad = pkg_cfg::pool()->emplace(
      "local.bad@r0",
      pkg_cfg::local_source{ .file_path = std::filesystem::path("dummy.lua") },
      "{}",
      std::nullopt,
      nullptr,
      nullptr,
      std::vector<pkg_cfg *>{},
      std::nullopt,
      std::filesystem::path{});
  bad->platforms.push_back(other);

  auto result{ engine_filter_host_platform({ good, bad }) };
  REQUIRE(result.size() == 1);
  CHECK(result[0] == good);
}

TEST_CASE("engine_filter_host_platform: multiple platforms one matching") {
  pkg_cfg *cfg = pkg_cfg::pool()->emplace(
      "local.tool@r0",
      pkg_cfg::local_source{ .file_path = std::filesystem::path("dummy.lua") },
      "{}",
      std::nullopt,
      nullptr,
      nullptr,
      std::vector<pkg_cfg *>{},
      std::nullopt,
      std::filesystem::path{});
  cfg->platforms.push_back("darwin");
  cfg->platforms.push_back("linux");
  cfg->platforms.push_back("windows");

  auto result{ engine_filter_host_platform({ cfg }) };
  CHECK(result.size() == 1);
}

TEST_CASE("engine_filter_host_platform: all filtered yields empty") {
  std::string const other{ std::string(platform::os_name()) == "darwin" ? "windows"
                                                                        : "darwin" };

  pkg_cfg *a = pkg_cfg::pool()->emplace(
      "local.a@r0",
      pkg_cfg::local_source{ .file_path = std::filesystem::path("dummy.lua") },
      "{}",
      std::nullopt,
      nullptr,
      nullptr,
      std::vector<pkg_cfg *>{},
      std::nullopt,
      std::filesystem::path{});
  a->platforms.push_back(other);

  pkg_cfg *b = pkg_cfg::pool()->emplace(
      "local.b@r0",
      pkg_cfg::local_source{ .file_path = std::filesystem::path("dummy.lua") },
      "{}",
      std::nullopt,
      nullptr,
      nullptr,
      std::vector<pkg_cfg *>{},
      std::nullopt,
      std::filesystem::path{});
  b->platforms.push_back(other);

  auto result{ engine_filter_host_platform({ a, b }) };
  CHECK(result.empty());
}

TEST_CASE("engine_filter_host_platform: empty input yields empty output") {
  auto result{ engine_filter_host_platform({}) };
  CHECK(result.empty());
}

TEST_CASE("engine_filter_host_platform: preserves order of matching cfgs") {
  pkg_cfg *a = pkg_cfg::pool()->emplace(
      "local.a@r0",
      pkg_cfg::local_source{ .file_path = std::filesystem::path("dummy.lua") },
      "{}",
      std::nullopt,
      nullptr,
      nullptr,
      std::vector<pkg_cfg *>{},
      std::nullopt,
      std::filesystem::path{});

  pkg_cfg *b = pkg_cfg::pool()->emplace(
      "local.b@r0",
      pkg_cfg::local_source{ .file_path = std::filesystem::path("dummy.lua") },
      "{}",
      std::nullopt,
      nullptr,
      nullptr,
      std::vector<pkg_cfg *>{},
      std::nullopt,
      std::filesystem::path{});

  pkg_cfg *c = pkg_cfg::pool()->emplace(
      "local.c@r0",
      pkg_cfg::local_source{ .file_path = std::filesystem::path("dummy.lua") },
      "{}",
      std::nullopt,
      nullptr,
      nullptr,
      std::vector<pkg_cfg *>{},
      std::nullopt,
      std::filesystem::path{});

  auto result{ engine_filter_host_platform({ a, b, c }) };
  REQUIRE(result.size() == 3);
  CHECK(result[0] == a);
  CHECK(result[1] == b);
  CHECK(result[2] == c);
}

}  // namespace envy
