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
                             std::filesystem::path const &file_path,
                             std::string serialized_options = "{}") {
  return recipe_spec::pool()->emplace(std::move(identity),
                                      recipe_spec::local_source{ file_path },
                                      std::move(serialized_options),
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

struct temp_dir_guard {
  std::filesystem::path path;
  explicit temp_dir_guard(std::filesystem::path p) : path(std::move(p)) {}
  ~temp_dir_guard() { std::filesystem::remove_all(path); }
};

void expect_user_managed_cache_phase_error(std::string const &phase_name) {
  cache c;
  engine eng{ c, std::nullopt };

  std::filesystem::path temp_dir{ std::filesystem::temp_directory_path() /
                                  ("envy_test_check_" + phase_name) };
  std::filesystem::create_directories(temp_dir);
  temp_dir_guard guard{ temp_dir };
  std::filesystem::path recipe_file{ temp_dir / "recipe.lua" };

  {
    std::ofstream ofs{ recipe_file };
    ofs << "IDENTITY = \"test.check_" << phase_name << "@v1\"\n";
    ofs << "CHECK = \"echo test\"\n";
    ofs << phase_name << " = function(ctx) end\n";
  }

  auto *spec{ make_local_spec("test.check_" + phase_name + "@v1", recipe_file) };
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
  CHECK(exception_msg.find("declares " + phase_name + " phase") != std::string::npos);
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
  expect_user_managed_cache_phase_error("FETCH");
}

TEST_CASE("user-managed package with check verb and stage phase throws parse error") {
  expect_user_managed_cache_phase_error("STAGE");
}

TEST_CASE("user-managed package with check verb and build phase throws parse error") {
  expect_user_managed_cache_phase_error("BUILD");
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

TEST_CASE("VALIDATE returns nil or true succeeds and sees options") {
  cache c;
  engine eng{ c, std::nullopt };

  std::filesystem::path temp_dir{ std::filesystem::temp_directory_path() /
                                  "envy_test_validate_ok" };
  std::filesystem::create_directories(temp_dir);
  temp_dir_guard guard{ temp_dir };
  std::filesystem::path recipe_file_ok{ temp_dir / "validate_ok.lua" };
  std::filesystem::path recipe_file_true{ temp_dir / "validate_true.lua" };
  {
    std::ofstream ofs{ recipe_file_ok };
    ofs << "IDENTITY = \"test.validate_ok@v1\"\n";
    ofs << "VALIDATE = function(opts) assert(opts.foo == \"bar\") end\n";
    ofs << "CHECK = function(ctx) return true end\n";
    ofs << "INSTALL = function(ctx) end\n";
    ofs.close();
  }

  recipe_file_true.replace_filename("validate_true.lua");
  {
    std::ofstream ofs{ recipe_file_true };
    ofs << "IDENTITY = \"test.validate_true@v1\"\n";
    ofs << "VALIDATE = function(opts) return true end\n";
    ofs << "CHECK = function(ctx) return true end\n";
    ofs << "INSTALL = function(ctx) end\n";
    ofs.close();
  }

  auto *spec{
    make_local_spec("test.validate_ok@v1", recipe_file_ok, "{ foo = \"bar\" }")
  };
  recipe *r{ eng.ensure_recipe(spec) };
  CHECK_NOTHROW(run_recipe_fetch_phase(r, eng));

  auto *spec_true{ make_local_spec("test.validate_true@v1", recipe_file_true) };
  recipe *r_true{ eng.ensure_recipe(spec_true) };
  CHECK_NOTHROW(run_recipe_fetch_phase(r_true, eng));
}

TEST_CASE("VALIDATE returns false fails") {
  cache c;
  engine eng{ c, std::nullopt };

  std::filesystem::path temp_dir{ std::filesystem::temp_directory_path() /
                                  "envy_test_validate_false" };
  std::filesystem::create_directories(temp_dir);
  temp_dir_guard guard{ temp_dir };
  std::filesystem::path recipe_file{ temp_dir / "validate_false.lua" };

  std::ofstream ofs{ recipe_file };
  ofs << "IDENTITY = \"test.validate_false@v1\"\n";
  ofs << "VALIDATE = function(opts) return false end\n";
  ofs << "CHECK = function(ctx) return true end\n";
  ofs << "INSTALL = function(ctx) end\n";
  ofs.close();

  auto *spec{ make_local_spec("test.validate_false@v1", recipe_file) };
  recipe *r{ eng.ensure_recipe(spec) };

  bool threw{ false };
  try {
    run_recipe_fetch_phase(r, eng);
  } catch (std::runtime_error const &e) {
    threw = true;
    std::string const msg{ e.what() };
    INFO(msg);
    CHECK(msg.find("returned false") != std::string::npos);
  }

  CHECK(threw);
}

TEST_CASE("VALIDATE returns string fails with message") {
  cache c;
  engine eng{ c, std::nullopt };

  std::filesystem::path temp_dir{ std::filesystem::temp_directory_path() /
                                  "envy_test_validate_string" };
  std::filesystem::create_directories(temp_dir);
  temp_dir_guard guard{ temp_dir };
  std::filesystem::path recipe_file{ temp_dir / "validate_string.lua" };

  std::ofstream ofs{ recipe_file };
  ofs << "IDENTITY = \"test.validate_string@v1\"\n";
  ofs << "VALIDATE = function(opts) return \"nope\" end\n";
  ofs << "CHECK = function(ctx) return true end\n";
  ofs << "INSTALL = function(ctx) end\n";
  ofs.close();

  auto *spec{ make_local_spec("test.validate_string@v1", recipe_file) };
  recipe *r{ eng.ensure_recipe(spec) };

  bool threw{ false };
  try {
    run_recipe_fetch_phase(r, eng);
  } catch (std::runtime_error const &e) {
    threw = true;
    std::string const msg{ e.what() };
    INFO(msg);
    CHECK(msg.find("nope") != std::string::npos);
  }
  CHECK(threw);
}

TEST_CASE("VALIDATE invalid return type errors") {
  cache c;
  engine eng{ c, std::nullopt };

  std::filesystem::path temp_dir{ std::filesystem::temp_directory_path() /
                                  "envy_test_validate_type" };
  std::filesystem::create_directories(temp_dir);
  temp_dir_guard guard{ temp_dir };
  std::filesystem::path recipe_file{ temp_dir / "validate_type.lua" };

  std::ofstream ofs{ recipe_file };
  ofs << "IDENTITY = \"test.validate_type@v1\"\n";
  ofs << "VALIDATE = function(opts) return 123 end\n";
  ofs << "CHECK = function(ctx) return true end\n";
  ofs << "INSTALL = function(ctx) end\n";
  ofs.close();

  auto *spec{ make_local_spec("test.validate_type@v1", recipe_file) };
  recipe *r{ eng.ensure_recipe(spec) };

  CHECK_THROWS(run_recipe_fetch_phase(r, eng));
}

TEST_CASE("VALIDATE set to non-function errors") {
  cache c;
  engine eng{ c, std::nullopt };

  std::filesystem::path temp_dir{ std::filesystem::temp_directory_path() /
                                  "envy_test_validate_nonfn" };
  std::filesystem::create_directories(temp_dir);
  temp_dir_guard guard{ temp_dir };
  std::filesystem::path recipe_file{ temp_dir / "validate_nonfn.lua" };

  std::ofstream ofs{ recipe_file };
  ofs << "IDENTITY = \"test.validate_nonfn@v1\"\n";
  ofs << "VALIDATE = 42\n";
  ofs << "CHECK = function(ctx) return true end\n";
  ofs << "INSTALL = function(ctx) end\n";
  ofs.close();

  auto *spec{ make_local_spec("test.validate_nonfn@v1", recipe_file) };
  recipe *r{ eng.ensure_recipe(spec) };

  CHECK_THROWS(run_recipe_fetch_phase(r, eng));
}

TEST_CASE("VALIDATE runtime error bubbles with context") {
  cache c;
  engine eng{ c, std::nullopt };

  std::filesystem::path temp_dir{ std::filesystem::temp_directory_path() /
                                  "envy_test_validate_error" };
  std::filesystem::create_directories(temp_dir);
  temp_dir_guard guard{ temp_dir };
  std::filesystem::path recipe_file{ temp_dir / "validate_error.lua" };

  std::ofstream ofs{ recipe_file };
  ofs << "IDENTITY = \"test.validate_error@v1\"\n";
  ofs << "VALIDATE = function(opts) error(\"boom\") end\n";
  ofs << "CHECK = function(ctx) return true end\n";
  ofs << "INSTALL = function(ctx) end\n";
  ofs.close();

  auto *spec{ make_local_spec("test.validate_error@v1", recipe_file) };
  recipe *r{ eng.ensure_recipe(spec) };

  bool threw{ false };
  try {
    run_recipe_fetch_phase(r, eng);
  } catch (std::runtime_error const &e) {
    threw = true;
    std::string const msg{ e.what() };
    INFO(msg);
    CHECK(msg.find("Lua error in test.validate_error@v1") != std::string::npos);
    CHECK(msg.find("boom") != std::string::npos);
  }
  CHECK(threw);
}

}  // namespace envy
