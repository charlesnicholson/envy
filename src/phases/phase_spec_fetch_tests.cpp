#include "phases/phase_spec_fetch.h"

#include "cache.h"
#include "engine.h"
#include "pkg.h"
#include "pkg_cfg.h"

#include "doctest.h"

#include <filesystem>
#include <fstream>

namespace envy {

namespace {

pkg_cfg *make_local_cfg(std::string identity,
                        std::filesystem::path const &file_path,
                        std::string serialized_options = "{}") {
  return pkg_cfg::pool()->emplace(std::move(identity),
                                  pkg_cfg::local_source{ file_path },
                                  std::move(serialized_options),
                                  std::nullopt,
                                  nullptr,
                                  nullptr,
                                  std::vector<pkg_cfg *>{},
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
  engine eng{ c };

  std::filesystem::path temp_dir{ std::filesystem::temp_directory_path() /
                                  ("envy_test_check_" + phase_name) };
  std::filesystem::create_directories(temp_dir);
  temp_dir_guard guard{ temp_dir };
  std::filesystem::path spec_file{ temp_dir / "spec.lua" };

  {
    std::ofstream ofs{ spec_file };
    ofs << "IDENTITY = \"test.check_" << phase_name << "@v1\"\n";
    ofs << "CHECK = \"echo test\"\n";
    ofs << phase_name << " = function(install_dir, stage_dir, fetch_dir, tmp_dir) end\n";
  }

  auto *cfg{ make_local_cfg("test.check_" + phase_name + "@v1", spec_file) };
  pkg *p{ eng.ensure_pkg(cfg) };

  bool exception_thrown = false;
  std::string exception_msg;
  try {
    run_spec_fetch_phase(p, eng);
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
  engine eng{ c };

  auto *cfg{ make_local_cfg("local.brew@r0", repo_path("examples/local.brew@r0.lua")) };
  pkg *p{ eng.ensure_pkg(cfg) };

  REQUIRE_NOTHROW(run_spec_fetch_phase(p, eng));
  CHECK(p->type == pkg_type::USER_MANAGED);
}

TEST_CASE("string check/install classified as user-managed") {
  cache c;
  engine eng{ c };

  auto *cfg{ make_local_cfg("local.string_check_install@v1",
                            repo_path("test_data/specs/string_check_install.lua")) };
  pkg *p{ eng.ensure_pkg(cfg) };

  REQUIRE_NOTHROW(run_spec_fetch_phase(p, eng));
  CHECK(p->type == pkg_type::USER_MANAGED);
}

TEST_CASE("string check without install errors") {
  cache c;
  engine eng{ c };

  auto *cfg{ make_local_cfg("local.string_check_only@v1",
                            repo_path("test_data/specs/string_check_only.lua")) };
  pkg *p{ eng.ensure_pkg(cfg) };

  CHECK_THROWS(run_spec_fetch_phase(p, eng));
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
  engine eng{ c };

  std::filesystem::path temp_dir{ std::filesystem::temp_directory_path() /
                                  "envy_test_check_install_ok" };
  std::filesystem::create_directories(temp_dir);
  std::filesystem::path spec_file{ temp_dir / "spec.lua" };

  std::ofstream ofs{ spec_file };
  ofs << "IDENTITY = \"test.check_install_ok@v1\"\n";
  ofs << "CHECK = \"echo test\"\n";
  ofs << "INSTALL = \"echo install\"\n";
  ofs.close();

  auto *cfg{ make_local_cfg("test.check_install_ok@v1", spec_file) };
  pkg *p{ eng.ensure_pkg(cfg) };

  // Should not throw - install phase is allowed with check verb
  CHECK_NOTHROW(run_spec_fetch_phase(p, eng));

  std::filesystem::remove_all(temp_dir);
}

TEST_CASE("VALIDATE returns nil or true succeeds and sees options") {
  cache c;
  engine eng{ c };

  std::filesystem::path temp_dir{ std::filesystem::temp_directory_path() /
                                  "envy_test_validate_ok" };
  std::filesystem::create_directories(temp_dir);
  temp_dir_guard guard{ temp_dir };
  std::filesystem::path spec_file_ok{ temp_dir / "validate_ok.lua" };
  std::filesystem::path spec_file_true{ temp_dir / "validate_true.lua" };
  {
    std::ofstream ofs{ spec_file_ok };
    ofs << "IDENTITY = \"test.validate_ok@v1\"\n";
    ofs << "VALIDATE = function(opts) assert(opts.foo == \"bar\") end\n";
    ofs << "CHECK = function(project_root) return true end\n";
    ofs << "INSTALL = function(install_dir, stage_dir, fetch_dir, tmp_dir) end\n";
    ofs.close();
  }

  spec_file_true.replace_filename("validate_true.lua");
  {
    std::ofstream ofs{ spec_file_true };
    ofs << "IDENTITY = \"test.validate_true@v1\"\n";
    ofs << "VALIDATE = function(opts) return true end\n";
    ofs << "CHECK = function(project_root) return true end\n";
    ofs << "INSTALL = function(install_dir, stage_dir, fetch_dir, tmp_dir) end\n";
    ofs.close();
  }

  auto *cfg{ make_local_cfg("test.validate_ok@v1", spec_file_ok, "{ foo = \"bar\" }") };
  pkg *p{ eng.ensure_pkg(cfg) };
  CHECK_NOTHROW(run_spec_fetch_phase(p, eng));

  auto *cfg_true{ make_local_cfg("test.validate_true@v1", spec_file_true) };
  pkg *p_true{ eng.ensure_pkg(cfg_true) };
  CHECK_NOTHROW(run_spec_fetch_phase(p_true, eng));
}

TEST_CASE("VALIDATE returns false fails") {
  cache c;
  engine eng{ c };

  std::filesystem::path temp_dir{ std::filesystem::temp_directory_path() /
                                  "envy_test_validate_false" };
  std::filesystem::create_directories(temp_dir);
  temp_dir_guard guard{ temp_dir };
  std::filesystem::path spec_file{ temp_dir / "validate_false.lua" };

  std::ofstream ofs{ spec_file };
  ofs << "IDENTITY = \"test.validate_false@v1\"\n";
  ofs << "VALIDATE = function(opts) return false end\n";
  ofs << "CHECK = function(project_root) return true end\n";
  ofs << "INSTALL = function(install_dir, stage_dir, fetch_dir, tmp_dir) end\n";
  ofs.close();

  auto *cfg{ make_local_cfg("test.validate_false@v1", spec_file) };
  pkg *p{ eng.ensure_pkg(cfg) };

  bool threw{ false };
  try {
    run_spec_fetch_phase(p, eng);
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
  engine eng{ c };

  std::filesystem::path temp_dir{ std::filesystem::temp_directory_path() /
                                  "envy_test_validate_string" };
  std::filesystem::create_directories(temp_dir);
  temp_dir_guard guard{ temp_dir };
  std::filesystem::path spec_file{ temp_dir / "validate_string.lua" };

  std::ofstream ofs{ spec_file };
  ofs << "IDENTITY = \"test.validate_string@v1\"\n";
  ofs << "VALIDATE = function(opts) return \"nope\" end\n";
  ofs << "CHECK = function(project_root) return true end\n";
  ofs << "INSTALL = function(install_dir, stage_dir, fetch_dir, tmp_dir) end\n";
  ofs.close();

  auto *cfg{ make_local_cfg("test.validate_string@v1", spec_file) };
  pkg *p{ eng.ensure_pkg(cfg) };

  bool threw{ false };
  try {
    run_spec_fetch_phase(p, eng);
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
  engine eng{ c };

  std::filesystem::path temp_dir{ std::filesystem::temp_directory_path() /
                                  "envy_test_validate_type" };
  std::filesystem::create_directories(temp_dir);
  temp_dir_guard guard{ temp_dir };
  std::filesystem::path spec_file{ temp_dir / "validate_type.lua" };

  std::ofstream ofs{ spec_file };
  ofs << "IDENTITY = \"test.validate_type@v1\"\n";
  ofs << "VALIDATE = function(opts) return 123 end\n";
  ofs << "CHECK = function(project_root) return true end\n";
  ofs << "INSTALL = function(install_dir, stage_dir, fetch_dir, tmp_dir) end\n";
  ofs.close();

  auto *cfg{ make_local_cfg("test.validate_type@v1", spec_file) };
  pkg *p{ eng.ensure_pkg(cfg) };

  CHECK_THROWS(run_spec_fetch_phase(p, eng));
}

TEST_CASE("VALIDATE set to non-function errors") {
  cache c;
  engine eng{ c };

  std::filesystem::path temp_dir{ std::filesystem::temp_directory_path() /
                                  "envy_test_validate_nonfn" };
  std::filesystem::create_directories(temp_dir);
  temp_dir_guard guard{ temp_dir };
  std::filesystem::path spec_file{ temp_dir / "validate_nonfn.lua" };

  std::ofstream ofs{ spec_file };
  ofs << "IDENTITY = \"test.validate_nonfn@v1\"\n";
  ofs << "VALIDATE = 42\n";
  ofs << "CHECK = function(project_root) return true end\n";
  ofs << "INSTALL = function(install_dir, stage_dir, fetch_dir, tmp_dir) end\n";
  ofs.close();

  auto *cfg{ make_local_cfg("test.validate_nonfn@v1", spec_file) };
  pkg *p{ eng.ensure_pkg(cfg) };

  CHECK_THROWS(run_spec_fetch_phase(p, eng));
}

TEST_CASE("VALIDATE runtime error bubbles with context") {
  cache c;
  engine eng{ c };

  std::filesystem::path temp_dir{ std::filesystem::temp_directory_path() /
                                  "envy_test_validate_error" };
  std::filesystem::create_directories(temp_dir);
  temp_dir_guard guard{ temp_dir };
  std::filesystem::path spec_file{ temp_dir / "validate_error.lua" };

  std::ofstream ofs{ spec_file };
  ofs << "IDENTITY = \"test.validate_error@v1\"\n";
  ofs << "VALIDATE = function(opts) error(\"boom\") end\n";
  ofs << "CHECK = function(project_root) return true end\n";
  ofs << "INSTALL = function(install_dir, stage_dir, fetch_dir, tmp_dir) end\n";
  ofs.close();

  auto *cfg{ make_local_cfg("test.validate_error@v1", spec_file) };
  pkg *p{ eng.ensure_pkg(cfg) };

  bool threw{ false };
  try {
    run_spec_fetch_phase(p, eng);
  } catch (std::runtime_error const &e) {
    threw = true;
    std::string const msg{ e.what() };
    INFO(msg);
    CHECK(msg.find("Lua error in test.validate_error@v1") != std::string::npos);
    CHECK(msg.find("boom") != std::string::npos);
  }
  CHECK(threw);
}

TEST_CASE("product name validation") {
  auto expect_valid{ [](std::string const &product_name) {
    cache c;
    engine eng{ c };

    std::filesystem::path temp_dir{ std::filesystem::temp_directory_path() /
                                    "envy_test_product_valid" };
    std::filesystem::create_directories(temp_dir);
    temp_dir_guard guard{ temp_dir };
    std::filesystem::path spec_file{ temp_dir / "spec.lua" };

    std::ofstream ofs{ spec_file };
    ofs << "IDENTITY = \"test.product@v1\"\n";
    ofs << "CHECK = function() return true end\n";
    ofs << "INSTALL = function() end\n";
    ofs << "PRODUCTS = { [\"" << product_name << "\"] = \"/usr/bin/tool\" }\n";
    ofs.close();

    auto *cfg{ make_local_cfg("test.product@v1", spec_file) };
    pkg *p{ eng.ensure_pkg(cfg) };

    CAPTURE(product_name);
    CHECK_NOTHROW(run_spec_fetch_phase(p, eng));
    CHECK(p->products.count(product_name) == 1);
  } };

  auto expect_rejected{ [](std::string const &lua_escaped_name) {
    cache c;
    engine eng{ c };

    std::filesystem::path temp_dir{ std::filesystem::temp_directory_path() /
                                    "envy_test_product_reject" };
    std::filesystem::create_directories(temp_dir);
    temp_dir_guard guard{ temp_dir };
    std::filesystem::path spec_file{ temp_dir / "spec.lua" };

    std::ofstream ofs{ spec_file };
    ofs << "IDENTITY = \"test.product@v1\"\n";
    ofs << "CHECK = function() return true end\n";
    ofs << "INSTALL = function() end\n";
    ofs << "PRODUCTS = { [\"" << lua_escaped_name << "\"] = \"/usr/bin/tool\" }\n";
    ofs.close();

    auto *cfg{ make_local_cfg("test.product@v1", spec_file) };
    pkg *p{ eng.ensure_pkg(cfg) };

    CAPTURE(lua_escaped_name);
    CHECK_THROWS_WITH(run_spec_fetch_phase(p, eng), doctest::Contains("shell-unsafe"));
  } };

  SUBCASE("valid") {
    expect_valid("tool");
    expect_valid("Tool123");
    expect_valid("my-tool");
    expect_valid("my_tool");
    expect_valid("arm-none-eabi-gcc");
    expect_valid("python3.14");
    expect_valid("g++");
    expect_valid("arm-none-eabi-c++");
    expect_valid("tool@v1");
    expect_valid("tool:variant");
    expect_valid("tool#tag");
    expect_valid("tool~beta");
    expect_valid("tool[0]");
  }

  SUBCASE("invalid") {
    expect_rejected("my tool");
    expect_rejected("tool$var");
    expect_rejected("tool`cmd`");
    expect_rejected("tool\\\"name");
    expect_rejected("tool'name");
    expect_rejected("tool%var%");
    expect_rejected("tool\\\\path");
    expect_rejected("tool!");
  }
}

TEST_CASE("PRODUCTS table syntax") {
  SUBCASE("string value produces script=true") {
    cache c;
    engine eng{ c };

    std::filesystem::path temp_dir{ std::filesystem::temp_directory_path() /
                                    "envy_test_products_string" };
    std::filesystem::create_directories(temp_dir);
    temp_dir_guard guard{ temp_dir };
    std::filesystem::path spec_file{ temp_dir / "spec.lua" };

    std::ofstream ofs{ spec_file };
    ofs << "IDENTITY = \"test.products@v1\"\n";
    ofs << "CHECK = function() return true end\n";
    ofs << "INSTALL = function() end\n";
    ofs << "PRODUCTS = { tool = \"/usr/bin/tool\" }\n";
    ofs.close();

    auto *cfg{ make_local_cfg("test.products@v1", spec_file) };
    pkg *p{ eng.ensure_pkg(cfg) };

    CHECK_NOTHROW(run_spec_fetch_phase(p, eng));
    CHECK(p->products.count("tool") == 1);
    CHECK(p->products.at("tool").value == "/usr/bin/tool");
    CHECK(p->products.at("tool").script == true);
  }

  SUBCASE("table value with script=true") {
    cache c;
    engine eng{ c };

    std::filesystem::path temp_dir{ std::filesystem::temp_directory_path() /
                                    "envy_test_products_table_true" };
    std::filesystem::create_directories(temp_dir);
    temp_dir_guard guard{ temp_dir };
    std::filesystem::path spec_file{ temp_dir / "spec.lua" };

    std::ofstream ofs{ spec_file };
    ofs << "IDENTITY = \"test.products@v1\"\n";
    ofs << "CHECK = function() return true end\n";
    ofs << "INSTALL = function() end\n";
    ofs << "PRODUCTS = { tool = { value = \"/usr/bin/tool\", script = true } }\n";
    ofs.close();

    auto *cfg{ make_local_cfg("test.products@v1", spec_file) };
    pkg *p{ eng.ensure_pkg(cfg) };

    CHECK_NOTHROW(run_spec_fetch_phase(p, eng));
    CHECK(p->products.count("tool") == 1);
    CHECK(p->products.at("tool").value == "/usr/bin/tool");
    CHECK(p->products.at("tool").script == true);
  }

  SUBCASE("table value with script=false") {
    cache c;
    engine eng{ c };

    std::filesystem::path temp_dir{ std::filesystem::temp_directory_path() /
                                    "envy_test_products_table_false" };
    std::filesystem::create_directories(temp_dir);
    temp_dir_guard guard{ temp_dir };
    std::filesystem::path spec_file{ temp_dir / "spec.lua" };

    std::ofstream ofs{ spec_file };
    ofs << "IDENTITY = \"test.products@v1\"\n";
    ofs << "CHECK = function() return true end\n";
    ofs << "INSTALL = function() end\n";
    ofs << "PRODUCTS = { lib = { value = \"/usr/lib/libfoo.so\", script = false } }\n";
    ofs.close();

    auto *cfg{ make_local_cfg("test.products@v1", spec_file) };
    pkg *p{ eng.ensure_pkg(cfg) };

    CHECK_NOTHROW(run_spec_fetch_phase(p, eng));
    CHECK(p->products.count("lib") == 1);
    CHECK(p->products.at("lib").value == "/usr/lib/libfoo.so");
    CHECK(p->products.at("lib").script == false);
  }

  SUBCASE("table value without script defaults to true") {
    cache c;
    engine eng{ c };

    std::filesystem::path temp_dir{ std::filesystem::temp_directory_path() /
                                    "envy_test_products_table_default" };
    std::filesystem::create_directories(temp_dir);
    temp_dir_guard guard{ temp_dir };
    std::filesystem::path spec_file{ temp_dir / "spec.lua" };

    std::ofstream ofs{ spec_file };
    ofs << "IDENTITY = \"test.products@v1\"\n";
    ofs << "CHECK = function() return true end\n";
    ofs << "INSTALL = function() end\n";
    ofs << "PRODUCTS = { tool = { value = \"/usr/bin/tool\" } }\n";
    ofs.close();

    auto *cfg{ make_local_cfg("test.products@v1", spec_file) };
    pkg *p{ eng.ensure_pkg(cfg) };

    CHECK_NOTHROW(run_spec_fetch_phase(p, eng));
    CHECK(p->products.count("tool") == 1);
    CHECK(p->products.at("tool").value == "/usr/bin/tool");
    CHECK(p->products.at("tool").script == true);
  }

  SUBCASE("table missing value field throws") {
    cache c;
    engine eng{ c };

    std::filesystem::path temp_dir{ std::filesystem::temp_directory_path() /
                                    "envy_test_products_missing_value" };
    std::filesystem::create_directories(temp_dir);
    temp_dir_guard guard{ temp_dir };
    std::filesystem::path spec_file{ temp_dir / "spec.lua" };

    std::ofstream ofs{ spec_file };
    ofs << "IDENTITY = \"test.products@v1\"\n";
    ofs << "CHECK = function() return true end\n";
    ofs << "INSTALL = function() end\n";
    ofs << "PRODUCTS = { lib = { script = false } }\n";
    ofs.close();

    auto *cfg{ make_local_cfg("test.products@v1", spec_file) };
    pkg *p{ eng.ensure_pkg(cfg) };

    CHECK_THROWS_WITH(run_spec_fetch_phase(p, eng),
                      doctest::Contains("must have string 'value' field"));
  }

  SUBCASE("table with invalid value type throws") {
    cache c;
    engine eng{ c };

    std::filesystem::path temp_dir{ std::filesystem::temp_directory_path() /
                                    "envy_test_products_invalid_value" };
    std::filesystem::create_directories(temp_dir);
    temp_dir_guard guard{ temp_dir };
    std::filesystem::path spec_file{ temp_dir / "spec.lua" };

    std::ofstream ofs{ spec_file };
    ofs << "IDENTITY = \"test.products@v1\"\n";
    ofs << "CHECK = function() return true end\n";
    ofs << "INSTALL = function() end\n";
    ofs << "PRODUCTS = { lib = { value = 123 } }\n";
    ofs.close();

    auto *cfg{ make_local_cfg("test.products@v1", spec_file) };
    pkg *p{ eng.ensure_pkg(cfg) };

    CHECK_THROWS_WITH(run_spec_fetch_phase(p, eng),
                      doctest::Contains("must have string 'value' field"));
  }

  SUBCASE("mixed string and table forms") {
    cache c;
    engine eng{ c };

    std::filesystem::path temp_dir{ std::filesystem::temp_directory_path() /
                                    "envy_test_products_mixed" };
    std::filesystem::create_directories(temp_dir);
    temp_dir_guard guard{ temp_dir };
    std::filesystem::path spec_file{ temp_dir / "spec.lua" };

    std::ofstream ofs{ spec_file };
    ofs << "IDENTITY = \"test.products@v1\"\n";
    ofs << "CHECK = function() return true end\n";
    ofs << "INSTALL = function() end\n";
    ofs << "PRODUCTS = {\n";
    ofs << "  tool = \"/usr/bin/tool\",\n";
    ofs << "  lib = { value = \"/usr/lib/libfoo.so\", script = false },\n";
    ofs << "}\n";
    ofs.close();

    auto *cfg{ make_local_cfg("test.products@v1", spec_file) };
    pkg *p{ eng.ensure_pkg(cfg) };

    CHECK_NOTHROW(run_spec_fetch_phase(p, eng));
    CHECK(p->products.count("tool") == 1);
    CHECK(p->products.at("tool").value == "/usr/bin/tool");
    CHECK(p->products.at("tool").script == true);
    CHECK(p->products.count("lib") == 1);
    CHECK(p->products.at("lib").value == "/usr/lib/libfoo.so");
    CHECK(p->products.at("lib").script == false);
  }
}

}  // namespace envy
