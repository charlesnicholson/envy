#include "manifest.h"

#include "doctest.h"

#include <filesystem>
#include <fstream>

namespace {

namespace fs = std::filesystem;

fs::path test_data_root() {
  auto root{ fs::current_path() / "test_data" / "manifest" };
  if (!fs::exists(root)) {
    root = fs::current_path().parent_path().parent_path() / "test_data" / "manifest";
  }
  return fs::absolute(root);
}

// Helper to change directory for a test scope
struct scoped_chdir {
  fs::path original;

  explicit scoped_chdir(fs::path const &target) : original(fs::current_path()) {
    fs::current_path(target);
  }

  ~scoped_chdir() { fs::current_path(original); }
};

}  // namespace

TEST_CASE("manifest::discover finds envy.lua in current directory") {
  auto test_root{ test_data_root() };
  auto repo_root{ test_root / "repo" };
  REQUIRE(fs::exists(repo_root / "envy.lua"));

  scoped_chdir cd{ repo_root };
  auto result{ envy::manifest::discover() };

  REQUIRE(result.has_value());
  CHECK(result->filename() == "envy.lua");
  CHECK(result->parent_path() == repo_root);
}

TEST_CASE("manifest::discover searches upward from subdirectory") {
  auto test_root{ test_data_root() };
  auto nested{ test_root / "repo" / "sibling" };
  REQUIRE(fs::exists(nested));

  scoped_chdir cd{ nested };
  auto result{ envy::manifest::discover() };

  REQUIRE(result.has_value());
  CHECK(result->filename() == "envy.lua");
  CHECK(result->parent_path() == test_root / "repo");
}

TEST_CASE("manifest::discover traverses through submodule (.git file)") {
  auto test_root{ test_data_root() };
  auto submodule_nested{ test_root / "repo" / "submodule" / "nested" };
  REQUIRE(fs::exists(submodule_nested));

  // Verify .git is a file (submodule), not directory
  auto git_file{ test_root / "repo" / "submodule" / ".git" };
  if (!fs::exists(git_file)) {
    // Fixture absent on this platform; create a placeholder .git file to emulate submodule
    // boundary.
    std::ofstream f{ git_file };
    f << "gitdir: ../.git/modules/submodule";
  }
  REQUIRE(fs::exists(git_file));
  REQUIRE(fs::is_regular_file(git_file));

  scoped_chdir cd{ submodule_nested };
  auto result{ envy::manifest::discover() };

  REQUIRE(result.has_value());
  CHECK(result->filename() == "envy.lua");
  CHECK(result->parent_path() == test_root / "repo");

  // Leave placeholder in place for subsequent test runs; fixture removal unnecessary.
}

TEST_CASE("manifest::discover stops at .git directory boundary") {
  // Create a temporary repo structure without envy.lua
  auto temp_root{ fs::temp_directory_path() / "envy-test-git-boundary" };
  fs::create_directories(temp_root / "test_repo" / ".git");
  fs::create_directories(temp_root / "test_repo" / "subdir");
  std::optional<std::optional<fs::path>> result_opt;  // wrapper to capture inside scope
  {
    scoped_chdir cd{ temp_root / "test_repo" / "subdir" };
    auto result{ envy::manifest::discover() };
    // Should stop at .git directory, not find anything
    CHECK_FALSE(result.has_value());
    result_opt = result;  // ensure scope ends (chdir restored) before removal
  }
  fs::remove_all(temp_root);
}

TEST_CASE("manifest::discover finds envy.lua in non-git directory") {
  auto test_root{ test_data_root() };
  auto non_git{ test_root / "non_git_dir" };
  REQUIRE(fs::exists(non_git / "envy.lua"));

  scoped_chdir cd{ non_git };
  auto result{ envy::manifest::discover() };

  REQUIRE(result.has_value());
  CHECK(result->filename() == "envy.lua");
  CHECK(result->parent_path() == non_git);
}

TEST_CASE("manifest::discover searches upward in non-git directory") {
  auto test_root{ test_data_root() };
  auto deeply_nested{ test_root / "non_git_dir" / "deeply" / "nested" / "path" };
  REQUIRE(fs::exists(deeply_nested));

  scoped_chdir cd{ deeply_nested };
  auto result{ envy::manifest::discover() };

  REQUIRE(result.has_value());
  CHECK(result->filename() == "envy.lua");
  CHECK(result->parent_path() == test_root / "non_git_dir");
}

TEST_CASE("manifest::discover returns nullopt when no envy.lua found") {
  // Use a system temp directory that's guaranteed to not have envy.lua
  auto temp_root{ fs::temp_directory_path() / "envy-test-no-manifest" };
  fs::create_directories(temp_root);
  std::optional<std::optional<fs::path>> result_opt;
  {
    scoped_chdir cd{ temp_root };
    auto result{ envy::manifest::discover() };
    CHECK_FALSE(result.has_value());
    result_opt = result;
  }
  fs::remove_all(temp_root);
}

// load tests -------------------------------------------------

TEST_CASE("manifest::load parses simple string package") {
  char const *script{ R"(
    packages = { { recipe = "arm.gcc@v2", file = "/fake/r.lua" } }
  )" };

  auto m{ envy::manifest::load(script, fs::path("/fake/envy.lua")) };

  REQUIRE(m->packages.size() == 1);
  CHECK(m->packages[0].identity == "arm.gcc@v2");
  CHECK(m->packages[0].is_local());
  CHECK(m->packages[0].options.empty());
}

TEST_CASE("manifest::load parses multiple string packages") {
  char const *script{ R"(
    packages = {
      { recipe = "arm.gcc@v2", file = "/fake/r.lua" },
      { recipe = "gnu.binutils@v3", file = "/fake/r.lua" },
      { recipe = "vendor.openocd@v1", file = "/fake/r.lua" }
    }
  )" };

  auto m{ envy::manifest::load(script, fs::path("/fake/envy.lua")) };

  REQUIRE(m->packages.size() == 3);
  CHECK(m->packages[0].identity == "arm.gcc@v2");
  CHECK(m->packages[1].identity == "gnu.binutils@v3");
  CHECK(m->packages[2].identity == "vendor.openocd@v1");
}

TEST_CASE("manifest::load parses table package with remote source") {
  char const *script{ R"(
    packages = {
      {
        recipe = "arm.gcc@v2",
        url = "https://example.com/gcc.lua",
        sha256 = "abc123"
      }
    }
  )" };

  auto m{ envy::manifest::load(script, fs::path("/fake/envy.lua")) };

  REQUIRE(m->packages.size() == 1);
  CHECK(m->packages[0].identity == "arm.gcc@v2");

  auto const *remote{ std::get_if<envy::recipe_spec::remote_source>(
      &m->packages[0].source) };
  REQUIRE(remote != nullptr);
  CHECK(remote->url == "https://example.com/gcc.lua");
  CHECK(remote->sha256 == "abc123");
}

TEST_CASE("manifest::load parses table package with local source") {
  char const *script{ R"(
    packages = {
      {
        recipe = "local.wrapper@v1",
        file = "./recipes/wrapper.lua"
      }
    }
  )" };

  auto m{ envy::manifest::load(script, fs::path("/project/envy.lua")) };

  REQUIRE(m->packages.size() == 1);
  CHECK(m->packages[0].identity == "local.wrapper@v1");

  auto const *local{ std::get_if<envy::recipe_spec::local_source>(&m->packages[0].source) };
  REQUIRE(local != nullptr);
  CHECK(local->file_path == fs::path("/project/recipes/wrapper.lua"));
}

TEST_CASE("manifest::load parses table package with options") {
  char const *script{ R"(
    packages = {
      {
        recipe = "arm.gcc@v2", file = "/fake/r.lua",
        options = {
          version = "13.2.0",
          target = "arm-none-eabi"
        }
      }
    }
  )" };

  auto m{ envy::manifest::load(script, fs::path("/fake/envy.lua")) };

  REQUIRE(m->packages.size() == 1);
  CHECK(m->packages[0].identity == "arm.gcc@v2");
  REQUIRE(m->packages[0].options.size() == 2);
  CHECK(*m->packages[0].options.at("version").get<std::string>() == "13.2.0");
  CHECK(*m->packages[0].options.at("target").get<std::string>() == "arm-none-eabi");
}

TEST_CASE("manifest::load parses mixed string and table packages") {
  char const *script{ R"(
    packages = {
      { recipe = "envy.homebrew@v4", file = "/fake/r.lua" },
      {
        recipe = "arm.gcc@v2",
        url = "https://example.com/gcc.lua",
        sha256 = "abc123",
        options = { version = "13.2.0" }
      },
      { recipe = "gnu.make@v1", file = "/fake/r.lua" }
    }
  )" };

  auto m{ envy::manifest::load(script, fs::path("/fake/envy.lua")) };

  REQUIRE(m->packages.size() == 3);
  CHECK(m->packages[0].identity == "envy.homebrew@v4");
  CHECK(m->packages[1].identity == "arm.gcc@v2");
  CHECK(m->packages[2].identity == "gnu.make@v1");
}

TEST_CASE("manifest::load allows platform conditionals") {
  char const *script{ R"(
    packages = {}
    if ENVY_PLATFORM == "darwin" then
      packages = { { recipe = "envy.homebrew@v4", file = "/fake/r.lua" } }
    elseif ENVY_PLATFORM == "linux" then
      packages = { { recipe = "system.apt@v1", file = "/fake/r.lua" } }
    elseif ENVY_PLATFORM == "windows" then
      packages = { { recipe = "system.choco@v1", file = "/fake/r.lua" } }
    end
  )" };

  auto m{ envy::manifest::load(script, fs::path("/fake/envy.lua")) };

  // Should have exactly one package based on current platform
  REQUIRE(m->packages.size() == 1);
#if defined(__APPLE__) && defined(__MACH__)
  CHECK(m->packages[0].identity == "envy.homebrew@v4");
#elif defined(__linux__)
  CHECK(m->packages[0].identity == "system.apt@v1");
#elif defined(_WIN32)
  CHECK(m->packages[0].identity == "system.choco@v1");
#endif
}

TEST_CASE("manifest::load stores manifest path") {
  char const *script{ "packages = {}" };

  auto m{ envy::manifest::load(script, fs::path("/some/project/envy.lua")) };

  CHECK(m->manifest_path == fs::path("/some/project/envy.lua"));
}

TEST_CASE("manifest::load resolves relative file paths") {
  char const *script{ R"(
    packages = {
      {
        recipe = "local.tool@v1",
        file = "../sibling/tool.lua"
      }
    }
  )" };

  auto m{ envy::manifest::load(script, fs::path("/project/sub/envy.lua")) };

  REQUIRE(m->packages.size() == 1);
  auto const *local{ std::get_if<envy::recipe_spec::local_source>(&m->packages[0].source) };
  REQUIRE(local != nullptr);
  CHECK(local->file_path == fs::path("/project/sibling/tool.lua"));
}

// Error cases ------------------------------------------------------------

TEST_CASE("manifest::load errors on missing packages global") {
  char const *script{ "-- no packages" };

  CHECK_THROWS_WITH_AS(envy::manifest::load(script, fs::path("/fake/envy.lua")),
                       "Manifest must define 'packages' global",
                       std::runtime_error);
}

TEST_CASE("manifest::load errors on non-table packages") {
  char const *script{ "packages = 'not a table'" };

  CHECK_THROWS_WITH_AS(envy::manifest::load(script, fs::path("/fake/envy.lua")),
                       "Global 'packages' is not a table",
                       std::runtime_error);
}

TEST_CASE("manifest::load errors on invalid package entry type") {
  char const *script{ "packages = { 123 }" };

  CHECK_THROWS_WITH_AS(envy::manifest::load(script, fs::path("/fake/envy.lua")),
                       "Recipe entry must be string or table",
                       std::runtime_error);
}

TEST_CASE("manifest::load errors on missing recipe field") {
  char const *script{ R"(
    packages = {
      { url = "https://example.com/foo.lua" }
    }
  )" };

  CHECK_THROWS_WITH_AS(envy::manifest::load(script, fs::path("/fake/envy.lua")),
                       "Recipe table missing required 'recipe' field",
                       std::runtime_error);
}

TEST_CASE("manifest::load errors on non-string recipe field") {
  char const *script{ R"(
    packages = {
      { recipe = 123 }
    }
  )" };

  CHECK_THROWS_WITH_AS(envy::manifest::load(script, fs::path("/fake/envy.lua")),
                       "Recipe 'recipe' field must be string",
                       std::runtime_error);
}

TEST_CASE("manifest::load errors on invalid recipe identity format") {
  char const *script{ R"(
    packages = { { recipe = "invalid-no-at-sign", file = "/fake/r.lua" } }
  )" };

  CHECK_THROWS_WITH_AS(envy::manifest::load(script, fs::path("/fake/envy.lua")),
                       "Invalid recipe identity format: invalid-no-at-sign",
                       std::runtime_error);
}

TEST_CASE("manifest::load errors on identity missing namespace") {
  char const *script{ R"(
    packages = { { recipe = "gcc@v2", file = "/fake/r.lua" } }
  )" };

  CHECK_THROWS_WITH_AS(envy::manifest::load(script, fs::path("/fake/envy.lua")),
                       "Invalid recipe identity format: gcc@v2",
                       std::runtime_error);
}

TEST_CASE("manifest::load errors on identity missing version") {
  char const *script{ R"(
    packages = { { recipe = "arm.gcc@", file = "/fake/r.lua" } }
  )" };

  CHECK_THROWS_WITH_AS(envy::manifest::load(script, fs::path("/fake/envy.lua")),
                       "Invalid recipe identity format: arm.gcc@",
                       std::runtime_error);
}

TEST_CASE("manifest::load errors on both url and file") {
  char const *script{ R"(
    packages = {
      {
        recipe = "arm.gcc@v2",
        url = "https://example.com/gcc.lua",
        file = "./local.lua"
      }
    }
  )" };

  CHECK_THROWS_WITH_AS(envy::manifest::load(script, fs::path("/fake/envy.lua")),
                       "Recipe cannot specify both 'url' and 'file'",
                       std::runtime_error);
}

TEST_CASE("manifest::load allows url without sha256 (permissive mode)") {
  char const *script{ R"(
    packages = {
      {
        recipe = "arm.gcc@v2",
        url = "https://example.com/gcc.lua"
      }
    }
  )" };

  auto const result{ envy::manifest::load(script, fs::path("/fake/envy.lua")) };
  REQUIRE(result->packages.size() == 1);
  CHECK(result->packages[0].identity == "arm.gcc@v2");
  CHECK(result->packages[0].is_remote());
  auto const *remote{ std::get_if<envy::recipe_spec::remote_source>(&result->packages[0].source) };
  REQUIRE(remote != nullptr);
  CHECK(remote->sha256.empty());  // No SHA256 provided (permissive)
}

TEST_CASE("manifest::load errors on non-string url") {
  char const *script{ R"(
    packages = {
      {
        recipe = "arm.gcc@v2",
        url = 123,
        sha256 = "abc"
      }
    }
  )" };

  CHECK_THROWS_WITH_AS(envy::manifest::load(script, fs::path("/fake/envy.lua")),
                       "Recipe 'url' field must be string",
                       std::runtime_error);
}

TEST_CASE("manifest::load errors on non-string sha256") {
  char const *script{ R"(
    packages = {
      {
        recipe = "arm.gcc@v2",
        url = "https://example.com/gcc.lua",
        sha256 = 123
      }
    }
  )" };

  CHECK_THROWS_WITH_AS(envy::manifest::load(script, fs::path("/fake/envy.lua")),
                       "Recipe 'sha256' field must be string",
                       std::runtime_error);
}

TEST_CASE("manifest::load errors on non-string file") {
  char const *script{ R"(
    packages = {
      {
        recipe = "local.tool@v1",
        file = 123
      }
    }
  )" };

  CHECK_THROWS_WITH_AS(envy::manifest::load(script, fs::path("/fake/envy.lua")),
                       "Recipe 'file' field must be string",
                       std::runtime_error);
}

TEST_CASE("manifest::load errors on non-table options") {
  char const *script{ R"(
    packages = {
      {
        recipe = "arm.gcc@v2",
        file = "/fake/r.lua",
        options = "not a table"
      }
    }
  )" };

  CHECK_THROWS_WITH_AS(envy::manifest::load(script, fs::path("/fake/envy.lua")),
                       "Recipe 'options' field must be table",
                       std::runtime_error);
}

TEST_CASE("manifest::load accepts non-string option values") {
  char const *script{ R"(
    packages = {
      {
        recipe = "arm.gcc@v2", file = "/fake/r.lua",
        options = { version = 123, debug = true, nested = { key = "value" } }
      }
    }
  )" };

  auto m{ envy::manifest::load(script, fs::path("/fake/envy.lua")) };

  REQUIRE(m->packages.size() == 1);
  REQUIRE(m->packages[0].options.size() == 3);
  CHECK(m->packages[0].options.at("version").is_integer());
  CHECK(*m->packages[0].options.at("version").get<int64_t>() == 123);
  CHECK(m->packages[0].options.at("debug").is_bool());
  CHECK(*m->packages[0].options.at("debug").get<bool>() == true);
  CHECK(m->packages[0].options.at("nested").is_table());
}

TEST_CASE("manifest::load allows same identity with different options") {
  char const *script{ R"(
    packages = {
      { recipe = "arm.gcc@v2", file = "/fake/r.lua", options = { version = "13.2.0" } },
      { recipe = "arm.gcc@v2", file = "/fake/r.lua", options = { version = "12.0.0" } }
    }
  )" };

  // Should not throw
  auto m{ envy::manifest::load(script, fs::path("/fake/envy.lua")) };
  REQUIRE(m->packages.size() == 2);
}

TEST_CASE("manifest::load allows duplicate packages") {
  char const *script{ R"(
    packages = {
      { recipe = "arm.gcc@v2", file = "/fake/r.lua" },
      { recipe = "arm.gcc@v2", file = "/fake/r.lua" }
    }
  )" };

  // Should not throw - duplicates are allowed, resolved during recipe resolution
  auto m{ envy::manifest::load(script, fs::path("/fake/envy.lua")) };
  REQUIRE(m->packages.size() == 2);
}

TEST_CASE("manifest::load errors on Lua syntax error") {
  CHECK_THROWS_AS(envy::manifest::load("packages = { this is not valid lua }",
                                       fs::path("/fake/envy.lua")),
                  std::runtime_error);
}

TEST_CASE("manifest::load errors on Lua runtime error") {
  CHECK_THROWS_AS(
      envy::manifest::load("error('intentional error')", fs::path("/fake/envy.lua")),
      std::runtime_error);
}
