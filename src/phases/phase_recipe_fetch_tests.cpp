#include "phase_recipe_fetch.h"

#include "cache.h"
#include "engine.h"
#include "recipe.h"
#include "recipe_spec.h"

#include "doctest.h"

#include <filesystem>
#include <fstream>

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

// ============================================================================
// User-managed package validation tests (check verb + cache phases)
// ============================================================================

TEST_CASE("user-managed package with check verb and fetch phase throws parse error") {
  cache c;
  engine eng{ c, std::nullopt };

  // Create temporary test recipe with check + fetch
  std::filesystem::path temp_dir{ std::filesystem::temp_directory_path() /
                                  "envy_test_check_fetch" };
  std::filesystem::create_directories(temp_dir);
  std::filesystem::path recipe_file{ temp_dir / "recipe.lua" };

  std::ofstream ofs{ recipe_file };
  ofs << "IDENTITY = \"test.check_fetch@v1\"\n";
  ofs << "CHECK = \"echo test\"\n";
  ofs << "FETCH = function(ctx) end\n";
  ofs.close();

  auto *spec{ make_local_spec("test.check_fetch@v1", recipe_file) };
  recipe *r{ eng.ensure_recipe(spec) };

  bool exception_thrown = false;
  std::string exception_msg;
  try {
    run_recipe_fetch_phase(r, eng);
  } catch (std::runtime_error const &e) {
    exception_thrown = true;
    exception_msg = e.what();
  }

  std::filesystem::remove_all(temp_dir);

  REQUIRE(exception_thrown);
  CHECK(exception_msg.find("has CHECK verb (user-managed)") != std::string::npos);
  CHECK(exception_msg.find("declares FETCH phase") != std::string::npos);
}

TEST_CASE("user-managed package with check verb and stage phase throws parse error") {
  cache c;
  engine eng{ c, std::nullopt };

  std::filesystem::path temp_dir{ std::filesystem::temp_directory_path() /
                                  "envy_test_check_stage" };
  std::filesystem::create_directories(temp_dir);
  std::filesystem::path recipe_file{ temp_dir / "recipe.lua" };

  std::ofstream ofs{ recipe_file };
  ofs << "IDENTITY = \"test.check_stage@v1\"\n";
  ofs << "CHECK = \"echo test\"\n";
  ofs << "STAGE = function(ctx) end\n";
  ofs.close();

  auto *spec{ make_local_spec("test.check_stage@v1", recipe_file) };
  recipe *r{ eng.ensure_recipe(spec) };

  bool exception_thrown = false;
  std::string exception_msg;
  try {
    run_recipe_fetch_phase(r, eng);
  } catch (std::runtime_error const &e) {
    exception_thrown = true;
    exception_msg = e.what();
  }

  std::filesystem::remove_all(temp_dir);

  REQUIRE(exception_thrown);
  CHECK(exception_msg.find("has CHECK verb (user-managed)") != std::string::npos);
  CHECK(exception_msg.find("declares STAGE phase") != std::string::npos);
}

TEST_CASE("user-managed package with check verb and build phase throws parse error") {
  cache c;
  engine eng{ c, std::nullopt };

  std::filesystem::path temp_dir{ std::filesystem::temp_directory_path() /
                                  "envy_test_check_build" };
  std::filesystem::create_directories(temp_dir);
  std::filesystem::path recipe_file{ temp_dir / "recipe.lua" };

  std::ofstream ofs{ recipe_file };
  ofs << "IDENTITY = \"test.check_build@v1\"\n";
  ofs << "CHECK = \"echo test\"\n";
  ofs << "BUILD = function(ctx) end\n";
  ofs.close();

  auto *spec{ make_local_spec("test.check_build@v1", recipe_file) };
  recipe *r{ eng.ensure_recipe(spec) };

  bool exception_thrown = false;
  std::string exception_msg;
  try {
    run_recipe_fetch_phase(r, eng);
  } catch (std::runtime_error const &e) {
    exception_thrown = true;
    exception_msg = e.what();
  }

  std::filesystem::remove_all(temp_dir);

  REQUIRE(exception_thrown);
  CHECK(exception_msg.find("has CHECK verb (user-managed)") != std::string::npos);
  CHECK(exception_msg.find("declares BUILD phase") != std::string::npos);
}

TEST_CASE("user-managed package with check verb and install phase succeeds") {
  cache c;
  engine eng{ c, std::nullopt };

  std::filesystem::path temp_dir{ std::filesystem::temp_directory_path() /
                                  "envy_test_check_install_ok" };
  std::filesystem::create_directories(temp_dir);
  std::filesystem::path recipe_file{ temp_dir / "recipe.lua" };

  std::ofstream ofs{ recipe_file };
  ofs << "IDENTITY = \"test.check_install_ok@v1\"\n";
  ofs << "CHECK = \"echo test\"\n";
  ofs << "INSTALL = \"echo install\"\n";
  ofs.close();

  auto *spec{ make_local_spec("test.check_install_ok@v1", recipe_file) };
  recipe *r{ eng.ensure_recipe(spec) };

  // Should not throw - install phase is allowed with check verb
  CHECK_NOTHROW(run_recipe_fetch_phase(r, eng));

  std::filesystem::remove_all(temp_dir);
}

}  // namespace envy
