#include "spec_util.h"

#include "doctest.h"

#include <filesystem>

namespace fs = std::filesystem;

TEST_CASE("extract_spec_identity returns IDENTITY from valid spec") {
  auto identity{ envy::extract_spec_identity(
      fs::path("test_data/spec_util/valid_spec.lua")) };

  CHECK(identity == "test.valid@v1");
}

TEST_CASE("extract_spec_identity throws on file not found") {
  CHECK_THROWS_WITH_AS(
      envy::extract_spec_identity(fs::path("test_data/spec_util/nonexistent.lua")),
      doctest::Contains("spec file not found"),
      std::runtime_error);
}

TEST_CASE("extract_spec_identity throws on missing IDENTITY") {
  CHECK_THROWS_WITH_AS(
      envy::extract_spec_identity(fs::path("test_data/spec_util/missing_identity.lua")),
      doctest::Contains("missing required IDENTITY field"),
      std::runtime_error);
}

TEST_CASE("extract_spec_identity throws on empty IDENTITY") {
  CHECK_THROWS_WITH_AS(
      envy::extract_spec_identity(fs::path("test_data/spec_util/empty_identity.lua")),
      doctest::Contains("IDENTITY cannot be empty"),
      std::runtime_error);
}

TEST_CASE("extract_spec_identity throws on non-string IDENTITY") {
  CHECK_THROWS_WITH_AS(
      envy::extract_spec_identity(fs::path("test_data/spec_util/non_string_identity.lua")),
      doctest::Contains("IDENTITY must be a string"),
      std::runtime_error);
}

TEST_CASE("extract_spec_identity throws on syntax error") {
  CHECK_THROWS_WITH_AS(
      envy::extract_spec_identity(fs::path("test_data/spec_util/syntax_error.lua")),
      doctest::Contains("failed to execute spec"),
      std::runtime_error);
}

TEST_CASE("extract_spec_identity with package_path_root enables bundle-local requires") {
  // This spec uses require("helpers") which only works if package.path is set correctly
  fs::path const bundle_root{ "test_data/spec_util/bundle_with_helper" };
  fs::path const spec_path{ bundle_root / "spec_using_helper.lua" };

  auto identity{ envy::extract_spec_identity(spec_path, bundle_root) };

  CHECK(identity == "test.using_helper@v1");
}

TEST_CASE(
    "extract_spec_identity without package_path_root fails on bundle-local requires") {
  // Without the package_path_root, require("helpers") should fail
  fs::path const spec_path{
    "test_data/spec_util/bundle_with_helper/spec_using_helper.lua"
  };

  CHECK_THROWS_WITH_AS(envy::extract_spec_identity(spec_path),  // no package_path_root
                       doctest::Contains("failed to execute spec"),
                       std::runtime_error);
}

TEST_CASE("extract_spec_identity works with existing bundle test data") {
  // Test with the actual bundle test data used by bundle_tests.cpp
  auto identity_a{ envy::extract_spec_identity(
      fs::path("test_data/bundles/simple-bundle/specs/spec_a.lua")) };
  auto identity_b{ envy::extract_spec_identity(
      fs::path("test_data/bundles/simple-bundle/specs/spec_b.lua")) };

  CHECK(identity_a == "test.spec_a@v1");
  CHECK(identity_b == "test.spec_b@v1");
}

TEST_CASE("extract_spec_identity provides envy globals at parse time") {
  // Specs may use envy.EXE_EXT, envy.PLATFORM, etc. at global scope
  // (e.g., PRODUCTS = { tool = "tool" .. envy.EXE_EXT })
  auto identity{ envy::extract_spec_identity(
      fs::path("test_data/spec_util/uses_envy_globals.lua")) };

  CHECK(identity == "test.uses_envy_globals@v1");
}
