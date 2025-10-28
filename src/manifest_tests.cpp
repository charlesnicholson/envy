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
    // Fixture absent on this platform; create a placeholder .git file to emulate submodule boundary.
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
