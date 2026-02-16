#include "manifest.h"

#include "sol/sol.hpp"

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
  auto result{ envy::manifest::discover(false) };

  REQUIRE(result.has_value());
  CHECK(result->filename() == "envy.lua");
  CHECK(result->parent_path() == repo_root);
}

TEST_CASE("manifest::discover searches upward from subdirectory") {
  auto test_root{ test_data_root() };
  auto nested{ test_root / "repo" / "sibling" };
  REQUIRE(fs::exists(nested));

  scoped_chdir cd{ nested };
  auto result{ envy::manifest::discover(false) };

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
  auto result{ envy::manifest::discover(false) };

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
    auto result{ envy::manifest::discover(false) };
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
  auto result{ envy::manifest::discover(false) };

  REQUIRE(result.has_value());
  CHECK(result->filename() == "envy.lua");
  CHECK(result->parent_path() == non_git);
}

TEST_CASE("manifest::discover searches upward in non-git directory") {
  auto test_root{ test_data_root() };
  auto deeply_nested{ test_root / "non_git_dir" / "deeply" / "nested" / "path" };
  REQUIRE(fs::exists(deeply_nested));

  scoped_chdir cd{ deeply_nested };
  auto result{ envy::manifest::discover(false) };

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
    auto result{ envy::manifest::discover(false) };
    CHECK_FALSE(result.has_value());
    result_opt = result;
  }
  fs::remove_all(temp_root);
}

// load tests -------------------------------------------------

TEST_CASE("manifest::load parses simple string package") {
  char const *script{ R"(
    -- @envy bin-dir "tools"
    PACKAGES = { { spec = "arm.gcc@v2", source = "/fake/r.lua" } }
  )" };

  auto m{ envy::manifest::load(script, fs::path("/fake/envy.lua")) };

  REQUIRE(m->packages.size() == 1);
  CHECK(m->packages[0]->identity == "arm.gcc@v2");
  CHECK(m->packages[0]->is_local());
  CHECK(m->packages[0]->serialized_options == "{}");
}

TEST_CASE("manifest::load parses multiple string packages") {
  char const *script{ R"(
    -- @envy bin-dir "tools"
    PACKAGES = {
      { spec = "arm.gcc@v2", source = "/fake/r.lua" },
      { spec = "gnu.binutils@v3", source = "/fake/r.lua" },
      { spec = "vendor.openocd@v1", source = "/fake/r.lua" }
    }
  )" };

  auto m{ envy::manifest::load(script, fs::path("/fake/envy.lua")) };

  REQUIRE(m->packages.size() == 3);
  CHECK(m->packages[0]->identity == "arm.gcc@v2");
  CHECK(m->packages[1]->identity == "gnu.binutils@v3");
  CHECK(m->packages[2]->identity == "vendor.openocd@v1");
}

TEST_CASE("manifest::load parses table package with remote source") {
  char const *script{ R"(
    -- @envy bin-dir "tools"
    PACKAGES = {
      {
        spec = "arm.gcc@v2",
        source = "https://example.com/gcc.lua",
        sha256 = "abc123"
      }
    }
  )" };

  auto m{ envy::manifest::load(script, fs::path("/fake/envy.lua")) };

  REQUIRE(m->packages.size() == 1);
  CHECK(m->packages[0]->identity == "arm.gcc@v2");

  auto const *remote{ std::get_if<envy::pkg_cfg::remote_source>(&m->packages[0]->source) };
  REQUIRE(remote != nullptr);
  CHECK(remote->url == "https://example.com/gcc.lua");
  CHECK(remote->sha256 == "abc123");
}

TEST_CASE("manifest::load parses table package with local source") {
  char const *script{ R"(
    -- @envy bin-dir "tools"
    PACKAGES = {
      {
        spec = "local.wrapper@v1",
        source = "./specs/wrapper.lua"
      }
    }
  )" };

  auto m{ envy::manifest::load(script, fs::path("/project/envy.lua")) };

  REQUIRE(m->packages.size() == 1);
  CHECK(m->packages[0]->identity == "local.wrapper@v1");

  auto const *local{ std::get_if<envy::pkg_cfg::local_source>(&m->packages[0]->source) };
  REQUIRE(local != nullptr);
  CHECK(local->file_path == fs::path("/project/specs/wrapper.lua"));
}

TEST_CASE("manifest::load parses table package with options") {
  char const *script{ R"(
    -- @envy bin-dir "tools"
    PACKAGES = {
      {
        spec = "arm.gcc@v2", source = "/fake/r.lua",
        options = {
          version = "13.2.0",
          target = "arm-none-eabi"
        }
      }
    }
  )" };

  auto m{ envy::manifest::load(script, fs::path("/fake/envy.lua")) };

  REQUIRE(m->packages.size() == 1);
  CHECK(m->packages[0]->identity == "arm.gcc@v2");

  // Deserialize and check
  sol::state lua;
  auto opts_result{ lua.safe_script("return " + m->packages[0]->serialized_options) };
  REQUIRE(opts_result.valid());
  sol::table opts = opts_result;
  CHECK(sol::object(opts["version"]).as<std::string>() == "13.2.0");
  CHECK(sol::object(opts["target"]).as<std::string>() == "arm-none-eabi");
}

TEST_CASE("manifest::load parses mixed string and table packages") {
  char const *script{ R"(
    -- @envy bin-dir "tools"
    PACKAGES = {
      { spec = "envy.homebrew@v4", source = "/fake/r.lua" },
      {
        spec = "arm.gcc@v2",
        source = "https://example.com/gcc.lua",
        sha256 = "abc123",
        options = { version = "13.2.0" }
      },
      { spec = "gnu.make@v1", source = "/fake/r.lua" }
    }
  )" };

  auto m{ envy::manifest::load(script, fs::path("/fake/envy.lua")) };

  REQUIRE(m->packages.size() == 3);
  CHECK(m->packages[0]->identity == "envy.homebrew@v4");
  CHECK(m->packages[1]->identity == "arm.gcc@v2");
  CHECK(m->packages[2]->identity == "gnu.make@v1");
}

TEST_CASE("manifest::load allows platform conditionals") {
  char const *script{ R"(
    -- @envy bin-dir "tools"
    PACKAGES = {}
    if envy.PLATFORM == "darwin" then
      PACKAGES = { { spec = "envy.homebrew@v4", source = "/fake/r.lua" } }
    elseif envy.PLATFORM == "linux" then
      PACKAGES = { { spec = "system.apt@v1", source = "/fake/r.lua" } }
    elseif envy.PLATFORM == "windows" then
      PACKAGES = { { spec = "system.choco@v1", source = "/fake/r.lua" } }
    end
  )" };

  auto m{ envy::manifest::load(script, fs::path("/fake/envy.lua")) };

  // Should have exactly one package based on current platform
  REQUIRE(m->packages.size() == 1);
#if defined(__APPLE__) && defined(__MACH__)
  CHECK(m->packages[0]->identity == "envy.homebrew@v4");
#elif defined(__linux__)
  CHECK(m->packages[0]->identity == "system.apt@v1");
#elif defined(_WIN32)
  CHECK(m->packages[0]->identity == "system.choco@v1");
#endif
}

TEST_CASE("manifest::load stores manifest path") {
  char const *script{ "-- @envy bin-dir \"tools\"\nPACKAGES = {}" };

  auto m{ envy::manifest::load(script, fs::path("/some/project/envy.lua")) };

  CHECK(m->manifest_path == fs::path("/some/project/envy.lua"));
}

TEST_CASE("manifest::load resolves relative file paths") {
  char const *script{ R"(
    -- @envy bin-dir "tools"
    PACKAGES = {
      {
        spec = "local.tool@v1",
        source = "../sibling/tool.lua"
      }
    }
  )" };

  auto m{ envy::manifest::load(script, fs::path("/project/sub/envy.lua")) };

  REQUIRE(m->packages.size() == 1);
  auto const *local{ std::get_if<envy::pkg_cfg::local_source>(&m->packages[0]->source) };
  REQUIRE(local != nullptr);
  CHECK(local->file_path == fs::path("/project/sibling/tool.lua"));
}

// Error cases ------------------------------------------------------------

TEST_CASE("manifest::load errors on missing packages global") {
  char const *script{ "-- @envy bin-dir \"tools\"\n-- no packages" };

  CHECK_THROWS_WITH_AS(envy::manifest::load(script, fs::path("/fake/envy.lua")),
                       "Manifest must define 'PACKAGES' global as a table",
                       std::runtime_error);
}

TEST_CASE("manifest::load errors on non-table packages") {
  char const *script{ "-- @envy bin-dir \"tools\"\nPACKAGES = 'not a table'" };

  CHECK_THROWS_WITH_AS(envy::manifest::load(script, fs::path("/fake/envy.lua")),
                       "Manifest must define 'PACKAGES' global as a table",
                       std::runtime_error);
}

TEST_CASE("manifest::load errors on invalid package entry type") {
  char const *script{ "-- @envy bin-dir \"tools\"\nPACKAGES = { 123 }" };

  CHECK_THROWS_WITH_AS(envy::manifest::load(script, fs::path("/fake/envy.lua")),
                       "Spec entry must be string or table",
                       std::runtime_error);
}

TEST_CASE("manifest::load errors on missing spec field") {
  char const *script{ R"(
    -- @envy bin-dir "tools"
    PACKAGES = {
      { source = "https://example.com/foo.lua" }
    }
  )" };

  CHECK_THROWS_WITH_AS(envy::manifest::load(script, fs::path("/fake/envy.lua")),
                       "Spec table missing required 'spec' field",
                       std::runtime_error);
}

TEST_CASE("manifest::load errors on non-string spec field") {
  char const *script{ R"(
    -- @envy bin-dir "tools"
    PACKAGES = {
      { spec = 123 }
    }
  )" };

  CHECK_THROWS_WITH_AS(envy::manifest::load(script, fs::path("/fake/envy.lua")),
                       "Spec: spec must be a string",
                       std::runtime_error);
}

TEST_CASE("manifest::load errors on invalid spec identity format") {
  char const *script{ R"(
    -- @envy bin-dir "tools"
    PACKAGES = { { spec = "invalid-no-at-sign", source = "/fake/r.lua" } }
  )" };

  CHECK_THROWS_WITH_AS(envy::manifest::load(script, fs::path("/fake/envy.lua")),
                       "Invalid spec identity format: invalid-no-at-sign",
                       std::runtime_error);
}

TEST_CASE("manifest::load errors on identity missing namespace") {
  char const *script{ R"(
    -- @envy bin-dir "tools"
    PACKAGES = { { spec = "gcc@v2", source = "/fake/r.lua" } }
  )" };

  CHECK_THROWS_WITH_AS(envy::manifest::load(script, fs::path("/fake/envy.lua")),
                       "Invalid spec identity format: gcc@v2",
                       std::runtime_error);
}

TEST_CASE("manifest::load errors on identity missing version") {
  char const *script{ R"(
    -- @envy bin-dir "tools"
    PACKAGES = { { spec = "arm.gcc@", source = "/fake/r.lua" } }
  )" };

  CHECK_THROWS_WITH_AS(envy::manifest::load(script, fs::path("/fake/envy.lua")),
                       "Invalid spec identity format: arm.gcc@",
                       std::runtime_error);
}

// Test removed - can no longer specify both url and file since we unified to 'source'

TEST_CASE("manifest::load allows url without sha256 (permissive mode)") {
  char const *script{ R"(
    -- @envy bin-dir "tools"
    PACKAGES = {
      {
        spec = "arm.gcc@v2",
        source = "https://example.com/gcc.lua"
      }
    }
  )" };

  auto const result{ envy::manifest::load(script, fs::path("/fake/envy.lua")) };
  REQUIRE(result->packages.size() == 1);
  CHECK(result->packages[0]->identity == "arm.gcc@v2");
  CHECK(result->packages[0]->is_remote());
  auto const *remote{ std::get_if<envy::pkg_cfg::remote_source>(
      &result->packages[0]->source) };
  REQUIRE(remote != nullptr);
  CHECK(remote->sha256.empty());  // No SHA256 provided (permissive)
}

TEST_CASE("manifest::load errors on non-string source") {
  char const *script{ R"(
    -- @envy bin-dir "tools"
    PACKAGES = {
      {
        spec = "arm.gcc@v2",
        source = 123,
        sha256 = "abc"
      }
    }
  )" };

  CHECK_THROWS_WITH_AS(envy::manifest::load(script, fs::path("/fake/envy.lua")),
                       "Spec 'source' field must be string or table",
                       std::runtime_error);
}

TEST_CASE("manifest::load errors on non-string sha256") {
  char const *script{ R"(
    -- @envy bin-dir "tools"
    PACKAGES = {
      {
        spec = "arm.gcc@v2",
        source = "https://example.com/gcc.lua",
        sha256 = 123
      }
    }
  )" };

  CHECK_THROWS_WITH_AS(envy::manifest::load(script, fs::path("/fake/envy.lua")),
                       "Spec source: sha256 must be a string",
                       std::runtime_error);
}

TEST_CASE("manifest::load errors on non-string source (local)") {
  char const *script{ R"(
    -- @envy bin-dir "tools"
    PACKAGES = {
      {
        spec = "local.tool@v1",
        source = 123
      }
    }
  )" };

  CHECK_THROWS_WITH_AS(envy::manifest::load(script, fs::path("/fake/envy.lua")),
                       "Spec 'source' field must be string or table",
                       std::runtime_error);
}

TEST_CASE("manifest::load errors on non-table options") {
  char const *script{ R"(
    -- @envy bin-dir "tools"
    PACKAGES = {
      {
        spec = "arm.gcc@v2",
        source = "/fake/r.lua",
        options = "not a table"
      }
    }
  )" };

  CHECK_THROWS_WITH_AS(envy::manifest::load(script, fs::path("/fake/envy.lua")),
                       "Spec 'options' field must be table",
                       std::runtime_error);
}

TEST_CASE("manifest::load accepts non-string option values") {
  char const *script{ R"(
    -- @envy bin-dir "tools"
    PACKAGES = {
      {
        spec = "arm.gcc@v2", source = "/fake/r.lua",
        options = { version = 123, debug = true, nested = { key = "value" } }
      }
    }
  )" };

  auto m{ envy::manifest::load(script, fs::path("/fake/envy.lua")) };

  REQUIRE(m->packages.size() == 1);

  // Deserialize and check
  sol::state lua;
  auto opts_result{ lua.safe_script("return " + m->packages[0]->serialized_options) };
  REQUIRE(opts_result.valid());
  sol::table opts = opts_result;
  CHECK(sol::object(opts["version"]).is<lua_Integer>());
  CHECK(sol::object(opts["version"]).as<int64_t>() == 123);
  CHECK(sol::object(opts["debug"]).is<bool>());
  CHECK(sol::object(opts["debug"]).as<bool>() == true);
  CHECK(sol::object(opts["nested"]).is<sol::table>());
}

TEST_CASE("manifest::load allows same identity with different options") {
  char const *script{ R"(
    -- @envy bin-dir "tools"
    PACKAGES = {
      { spec = "arm.gcc@v2", source = "/fake/r.lua", options = { version = "13.2.0" } },
      { spec = "arm.gcc@v2", source = "/fake/r.lua", options = { version = "12.0.0" } }
    }
  )" };

  // Should not throw
  auto m{ envy::manifest::load(script, fs::path("/fake/envy.lua")) };
  REQUIRE(m->packages.size() == 2);
}

TEST_CASE("manifest::load allows duplicate packages") {
  char const *script{ R"(
    -- @envy bin-dir "tools"
    PACKAGES = {
      { spec = "arm.gcc@v2", source = "/fake/r.lua" },
      { spec = "arm.gcc@v2", source = "/fake/r.lua" }
    }
  )" };

  // Should not throw - duplicates are allowed, resolved during spec resolution
  auto m{ envy::manifest::load(script, fs::path("/fake/envy.lua")) };
  REQUIRE(m->packages.size() == 2);
}

TEST_CASE("manifest::load errors on Lua syntax error") {
  // Note: Lua syntax errors occur before bin-dir validation, so no bin-dir needed
  CHECK_THROWS_AS(envy::manifest::load("-- @envy bin-dir \"tools\"\n"
                                       "PACKAGES = { this is not valid lua }",
                                       fs::path("/fake/envy.lua")),
                  std::runtime_error);
}

TEST_CASE("manifest::load errors on Lua runtime error") {
  // Note: Lua runtime errors occur after Lua execution, so bin-dir is needed
  CHECK_THROWS_AS(
      envy::manifest::load("-- @envy bin-dir \"tools\"\nerror('intentional error')",
                           fs::path("/fake/envy.lua")),
      std::runtime_error);
}

// @envy directive parsing tests -------------------------------------------

TEST_CASE("parse_envy_meta extracts version") {
  auto directives{ envy::parse_envy_meta(R"(
-- @envy version "1.2.3"
PACKAGES = {}
)") };

  REQUIRE(directives.version.has_value());
  CHECK(*directives.version == "1.2.3");
  CHECK_FALSE(directives.cache.has_value());
  CHECK_FALSE(directives.mirror.has_value());
}

TEST_CASE("parse_envy_meta extracts all directives") {
#ifdef _WIN32
  auto directives{ envy::parse_envy_meta(R"(
-- @envy version "2.0.0"
-- @envy cache-win "C:\opt\envy-cache"
-- @envy mirror "https://internal.corp/releases"
PACKAGES = {}
)") };
  CHECK(*directives.cache == "C:\\opt\\envy-cache");
#else
  auto directives{ envy::parse_envy_meta(R"(
-- @envy version "2.0.0"
-- @envy cache-posix "/opt/envy-cache"
-- @envy mirror "https://internal.corp/releases"
PACKAGES = {}
)") };
  CHECK(*directives.cache == "/opt/envy-cache");
#endif

  REQUIRE(directives.version.has_value());
  CHECK(*directives.version == "2.0.0");
  REQUIRE(directives.cache.has_value());
  REQUIRE(directives.mirror.has_value());
  CHECK(*directives.mirror == "https://internal.corp/releases");
}

TEST_CASE("parse_envy_meta handles escaped quotes") {
  auto directives{ envy::parse_envy_meta(R"(
-- @envy version "1.0.0-\"beta\""
PACKAGES = {}
)") };

  REQUIRE(directives.version.has_value());
  CHECK(*directives.version == "1.0.0-\"beta\"");
}

TEST_CASE("parse_envy_meta handles escaped backslash") {
#ifdef _WIN32
  auto directives{ envy::parse_envy_meta(R"(
-- @envy cache-win "C:\\Users\\test\\cache"
PACKAGES = {}
)") };
  REQUIRE(directives.cache.has_value());
  CHECK(*directives.cache == "C:\\Users\\test\\cache");
#else
  // Test backslash escaping in version string on POSIX (cache-posix wouldn't have
  // backslashes)
  auto directives{ envy::parse_envy_meta(R"(
-- @envy version "1.0.0-with\\backslash"
PACKAGES = {}
)") };
  REQUIRE(directives.version.has_value());
  CHECK(*directives.version == "1.0.0-with\\backslash");
#endif
}

TEST_CASE("parse_envy_meta handles mixed escapes") {
  auto directives{ envy::parse_envy_meta(R"(
-- @envy version "test-\"quoted\"-and-\\backslash"
PACKAGES = {}
)") };

  REQUIRE(directives.version.has_value());
  CHECK(*directives.version == "test-\"quoted\"-and-\\backslash");
}

TEST_CASE("parse_envy_meta returns empty for missing directives") {
  auto directives{ envy::parse_envy_meta(R"(
-- This manifest has no @envy directives
PACKAGES = {}
)") };

  CHECK_FALSE(directives.version.has_value());
  CHECK_FALSE(directives.cache.has_value());
  CHECK_FALSE(directives.mirror.has_value());
}

TEST_CASE("parse_envy_meta handles whitespace variants") {
#ifdef _WIN32
  auto directives{ envy::parse_envy_meta(
      "--   @envy   version   \"1.0.0\"\n"
      "--\t@envy\tcache-win\t\"C:\\path\"\n"
      "PACKAGES = {}\n") };
  CHECK(*directives.cache == "C:\\path");
#else
  auto directives{ envy::parse_envy_meta(
      "--   @envy   version   \"1.0.0\"\n"
      "--\t@envy\tcache-posix\t\"/path\"\n"
      "PACKAGES = {}\n") };
  CHECK(*directives.cache == "/path");
#endif

  REQUIRE(directives.version.has_value());
  CHECK(*directives.version == "1.0.0");
  REQUIRE(directives.cache.has_value());
}

TEST_CASE("parse_envy_meta finds directives anywhere in file") {
  std::string script;
  for (int i = 0; i < 50; ++i) { script += "-- line " + std::to_string(i) + "\n"; }
  script += "-- @envy version \"deep-in-file\"\n";
  script += "PACKAGES = {}\n";

  auto const meta{ envy::parse_envy_meta(script) };

  REQUIRE(meta.version.has_value());
  CHECK(*meta.version == "deep-in-file");
}

TEST_CASE("parse_envy_meta ignores unknown directives") {
  auto directives{ envy::parse_envy_meta(R"(
-- @envy version "1.0.0"
-- @envy unknown "some-value"
-- @envy future_directive "another-value"
PACKAGES = {}
)") };

  REQUIRE(directives.version.has_value());
  CHECK(*directives.version == "1.0.0");
  // Unknown directives silently ignored
}

TEST_CASE("manifest::load populates directives field") {
#ifdef _WIN32
  char const *script{ R"(
-- @envy version "1.2.3"
-- @envy bin-dir "tools"
-- @envy cache-win "C:\custom\cache"
PACKAGES = {}
)" };
  char const *expected_cache{ "C:\\custom\\cache" };
#else
  char const *script{ R"(
-- @envy version "1.2.3"
-- @envy bin-dir "tools"
-- @envy cache-posix "/custom/cache"
PACKAGES = {}
)" };
  char const *expected_cache{ "/custom/cache" };
#endif

  auto m{ envy::manifest::load(script, fs::path("/fake/envy.lua")) };

  REQUIRE(m->meta.version.has_value());
  CHECK(*m->meta.version == "1.2.3");
  REQUIRE(m->meta.bin.has_value());
  CHECK(*m->meta.bin == "tools");
  REQUIRE(m->meta.cache.has_value());
  CHECK(*m->meta.cache == expected_cache);
  CHECK_FALSE(m->meta.mirror.has_value());
}

TEST_CASE("parse_envy_meta extracts bin") {
  auto directives{ envy::parse_envy_meta(R"(
-- @envy bin "tools/bin"
PACKAGES = {}
)") };

  REQUIRE(directives.bin.has_value());
  CHECK(*directives.bin == "tools/bin");
}

TEST_CASE("parse_envy_meta extracts bin-dir as legacy alias") {
  auto directives{ envy::parse_envy_meta(R"(
-- @envy bin-dir "legacy/path"
PACKAGES = {}
)") };

  REQUIRE(directives.bin.has_value());
  CHECK(*directives.bin == "legacy/path");
}

TEST_CASE("parse_envy_meta extracts bin with path separators") {
  auto directives{ envy::parse_envy_meta(R"(
-- @envy bin "../sibling/tools"
PACKAGES = {}
)") };

  REQUIRE(directives.bin.has_value());
  CHECK(*directives.bin == "../sibling/tools");
}

TEST_CASE("manifest::load errors on missing bin directive") {
  char const *script{ R"(
-- @envy version "1.0.0"
PACKAGES = {}
)" };

  CHECK_THROWS_WITH_AS(envy::manifest::load(script, fs::path("/fake/envy.lua")),
                       "Manifest missing required '@envy bin' directive.\n"
                       "Add to manifest header, e.g.: -- @envy bin \"tools\"",
                       std::runtime_error);
}

// ============================================================================
// @envy deploy directive tests
// ============================================================================

TEST_CASE("parse_envy_meta extracts deploy true") {
  auto directives{ envy::parse_envy_meta(R"(
-- @envy bin "tools"
-- @envy deploy "true"
PACKAGES = {}
)") };

  REQUIRE(directives.deploy.has_value());
  CHECK(*directives.deploy == true);
}

TEST_CASE("parse_envy_meta extracts deploy false") {
  auto directives{ envy::parse_envy_meta(R"(
-- @envy bin "tools"
-- @envy deploy "false"
PACKAGES = {}
)") };

  REQUIRE(directives.deploy.has_value());
  CHECK(*directives.deploy == false);
}

TEST_CASE("parse_envy_meta deploy absent yields nullopt") {
  auto directives{ envy::parse_envy_meta(R"(
-- @envy bin "tools"
PACKAGES = {}
)") };

  CHECK_FALSE(directives.deploy.has_value());
}

TEST_CASE("parse_envy_meta ignores invalid deploy value") {
  auto directives{ envy::parse_envy_meta(R"(
-- @envy bin "tools"
-- @envy deploy "invalid"
PACKAGES = {}
)") };

  // Invalid boolean strings result in nullopt
  CHECK_FALSE(directives.deploy.has_value());
}

// ============================================================================
// @envy root directive tests
// ============================================================================

TEST_CASE("parse_envy_meta extracts root true") {
  auto directives{ envy::parse_envy_meta(R"(
-- @envy bin "tools"
-- @envy root "true"
PACKAGES = {}
)") };

  REQUIRE(directives.root.has_value());
  CHECK(*directives.root == true);
}

TEST_CASE("parse_envy_meta extracts root false") {
  auto directives{ envy::parse_envy_meta(R"(
-- @envy bin "tools"
-- @envy root "false"
PACKAGES = {}
)") };

  REQUIRE(directives.root.has_value());
  CHECK(*directives.root == false);
}

TEST_CASE("parse_envy_meta root absent yields nullopt") {
  auto directives{ envy::parse_envy_meta(R"(
-- @envy bin "tools"
PACKAGES = {}
)") };

  CHECK_FALSE(directives.root.has_value());
}

TEST_CASE("parse_envy_meta ignores invalid root value") {
  auto directives{ envy::parse_envy_meta(R"(
-- @envy bin "tools"
-- @envy root "maybe"
PACKAGES = {}
)") };

  // Invalid boolean strings result in nullopt
  CHECK_FALSE(directives.root.has_value());
}

// ============================================================================
// manifest::discover() with root directive tests
// ============================================================================

TEST_CASE("manifest::discover with root=false continues search upward") {
  // Create temp structure: parent/child where child has root=false
  auto temp_root{ fs::canonical(fs::temp_directory_path()) / "envy-test-root-false" };
  auto parent_dir{ temp_root / "parent" };
  auto child_dir{ parent_dir / "child" };

  fs::create_directories(child_dir);

  // Parent manifest: root=true (default, stops search)
  std::ofstream parent_manifest{ parent_dir / "envy.lua" };
  parent_manifest << "-- @envy bin \"tools\"\nPACKAGES = {}\n";
  parent_manifest.close();

  // Child manifest: root=false (continues search)
  std::ofstream child_manifest{ child_dir / "envy.lua" };
  child_manifest << "-- @envy bin \"tools\"\n-- @envy root \"false\"\nPACKAGES = {}\n";
  child_manifest.close();

  {
    scoped_chdir cd{ child_dir };
    auto result{ envy::manifest::discover(false) };

    // Should find parent (root=true) instead of child (root=false)
    REQUIRE(result.has_value());
    CHECK(result->parent_path() == parent_dir);
  }

  fs::remove_all(temp_root);
}

TEST_CASE("manifest::discover with root=true stops immediately") {
  auto temp_root{ fs::canonical(fs::temp_directory_path()) / "envy-test-root-true" };
  auto parent_dir{ temp_root / "parent" };
  auto child_dir{ parent_dir / "child" };

  fs::create_directories(child_dir);

  // Parent manifest
  std::ofstream parent_manifest{ parent_dir / "envy.lua" };
  parent_manifest << "-- @envy bin \"tools\"\nPACKAGES = {}\n";
  parent_manifest.close();

  // Child manifest: root=true (default)
  std::ofstream child_manifest{ child_dir / "envy.lua" };
  child_manifest << "-- @envy bin \"tools\"\n-- @envy root \"true\"\nPACKAGES = {}\n";
  child_manifest.close();

  {
    scoped_chdir cd{ child_dir };
    auto result{ envy::manifest::discover(false) };

    // Should stop at child (root=true)
    REQUIRE(result.has_value());
    CHECK(result->parent_path() == child_dir);
  }

  fs::remove_all(temp_root);
}

TEST_CASE("manifest::discover with all root=false uses closest to filesystem root") {
  // Create 3-level structure: grandparent/parent/child, all root=false
  auto temp_root{ fs::canonical(fs::temp_directory_path()) / "envy-test-all-root-false" };
  auto grandparent_dir{ temp_root / "grandparent" };
  auto parent_dir{ grandparent_dir / "parent" };
  auto child_dir{ parent_dir / "child" };

  fs::create_directories(child_dir);

  // All manifests have root=false
  std::ofstream grandparent_manifest{ grandparent_dir / "envy.lua" };
  grandparent_manifest
      << "-- @envy bin \"tools\"\n-- @envy root \"false\"\nPACKAGES = {}\n";
  grandparent_manifest.close();

  std::ofstream parent_manifest{ parent_dir / "envy.lua" };
  parent_manifest << "-- @envy bin \"tools\"\n-- @envy root \"false\"\nPACKAGES = {}\n";
  parent_manifest.close();

  std::ofstream child_manifest{ child_dir / "envy.lua" };
  child_manifest << "-- @envy bin \"tools\"\n-- @envy root \"false\"\nPACKAGES = {}\n";
  child_manifest.close();

  {
    scoped_chdir cd{ child_dir };
    auto result{ envy::manifest::discover(false) };

    // Should use grandparent (closest to filesystem root among non-roots)
    REQUIRE(result.has_value());
    CHECK(result->parent_path() == grandparent_dir);
  }

  fs::remove_all(temp_root);
}

TEST_CASE("manifest::discover F-T-F uses middle (root=true) manifest") {
  // 3-level: grandparent(F) -> parent(T) -> child(F)
  auto temp_root{ fs::canonical(fs::temp_directory_path()) / "envy-test-ftf" };
  auto grandparent_dir{ temp_root / "grandparent" };
  auto parent_dir{ grandparent_dir / "parent" };
  auto child_dir{ parent_dir / "child" };

  fs::create_directories(child_dir);

  std::ofstream grandparent_manifest{ grandparent_dir / "envy.lua" };
  grandparent_manifest
      << "-- @envy bin \"tools\"\n-- @envy root \"false\"\nPACKAGES = {}\n";
  grandparent_manifest.close();

  std::ofstream parent_manifest{ parent_dir / "envy.lua" };
  parent_manifest << "-- @envy bin \"tools\"\n-- @envy root \"true\"\nPACKAGES = {}\n";
  parent_manifest.close();

  std::ofstream child_manifest{ child_dir / "envy.lua" };
  child_manifest << "-- @envy bin \"tools\"\n-- @envy root \"false\"\nPACKAGES = {}\n";
  child_manifest.close();

  {
    scoped_chdir cd{ child_dir };
    auto result{ envy::manifest::discover(false) };

    // Should stop at parent (root=true)
    REQUIRE(result.has_value());
    CHECK(result->parent_path() == parent_dir);
  }

  fs::remove_all(temp_root);
}

TEST_CASE("manifest::discover F-F with no grandparent uses parent") {
  // 2-level: parent(F) -> child(F), no grandparent manifest
  auto temp_root{ fs::canonical(fs::temp_directory_path()) /
                  "envy-test-ff-no-grandparent" };
  auto parent_dir{ temp_root / "parent" };
  auto child_dir{ parent_dir / "child" };

  fs::create_directories(child_dir);

  std::ofstream parent_manifest{ parent_dir / "envy.lua" };
  parent_manifest << "-- @envy bin \"tools\"\n-- @envy root \"false\"\nPACKAGES = {}\n";
  parent_manifest.close();

  std::ofstream child_manifest{ child_dir / "envy.lua" };
  child_manifest << "-- @envy bin \"tools\"\n-- @envy root \"false\"\nPACKAGES = {}\n";
  child_manifest.close();

  {
    scoped_chdir cd{ child_dir };
    auto result{ envy::manifest::discover(false) };

    // Should use parent (closest to root among non-roots, no grandparent manifest exists)
    REQUIRE(result.has_value());
    CHECK(result->parent_path() == parent_dir);
  }

  fs::remove_all(temp_root);
}

TEST_CASE("manifest::discover F with no parent skips to grandparent") {
  // 3-level: grandparent(F) -> parent(no manifest) -> child(F)
  auto temp_root{ fs::canonical(fs::temp_directory_path()) /
                  "envy-test-f-skip-grandparent" };
  auto grandparent_dir{ temp_root / "grandparent" };
  auto parent_dir{ grandparent_dir / "parent" };
  auto child_dir{ parent_dir / "child" };

  fs::create_directories(child_dir);

  std::ofstream grandparent_manifest{ grandparent_dir / "envy.lua" };
  grandparent_manifest
      << "-- @envy bin \"tools\"\n-- @envy root \"false\"\nPACKAGES = {}\n";
  grandparent_manifest.close();

  // No manifest in parent_dir

  std::ofstream child_manifest{ child_dir / "envy.lua" };
  child_manifest << "-- @envy bin \"tools\"\n-- @envy root \"false\"\nPACKAGES = {}\n";
  child_manifest.close();

  {
    scoped_chdir cd{ child_dir };
    auto result{ envy::manifest::discover(false) };

    // Should use grandparent (closest to root, skipping parent which has no manifest)
    REQUIRE(result.has_value());
    CHECK(result->parent_path() == grandparent_dir);
  }

  fs::remove_all(temp_root);
}

TEST_CASE("manifest::discover with only child manifest (root=false) uses child") {
  // Single manifest with root=false, no parents
  auto temp_root{ fs::canonical(fs::temp_directory_path()) /
                  "envy-test-single-root-false" };
  auto child_dir{ temp_root / "child" };

  fs::create_directories(child_dir);

  std::ofstream child_manifest{ child_dir / "envy.lua" };
  child_manifest << "-- @envy bin \"tools\"\n-- @envy root \"false\"\nPACKAGES = {}\n";
  child_manifest.close();

  {
    scoped_chdir cd{ child_dir };
    auto result{ envy::manifest::discover(false) };

    // Should use child even though root=false (only manifest in tree)
    REQUIRE(result.has_value());
    CHECK(result->parent_path() == child_dir);
  }

  fs::remove_all(temp_root);
}

// ============================================================================
// manifest::discover(nearest=true) tests
// ============================================================================

TEST_CASE("manifest::discover nearest returns first envy.lua found") {
  // Create parent/child where child has root=false, parent has root=true
  auto temp_root{ fs::canonical(fs::temp_directory_path()) / "envy-test-nearest-basic" };
  auto parent_dir{ temp_root / "parent" };
  auto child_dir{ parent_dir / "child" };

  fs::create_directories(child_dir);

  std::ofstream parent_manifest{ parent_dir / "envy.lua" };
  parent_manifest << "-- @envy bin \"tools\"\nPACKAGES = {}\n";
  parent_manifest.close();

  std::ofstream child_manifest{ child_dir / "envy.lua" };
  child_manifest << "-- @envy bin \"tools\"\n-- @envy root \"false\"\nPACKAGES = {}\n";
  child_manifest.close();

  {
    scoped_chdir cd{ child_dir };
    // Normal discover walks up to parent (root=true)
    auto normal_result{ envy::manifest::discover(false) };
    REQUIRE(normal_result.has_value());
    CHECK(normal_result->parent_path() == parent_dir);

    // Nearest discover returns child immediately
    auto nearest_result{ envy::manifest::discover(true) };
    REQUIRE(nearest_result.has_value());
    CHECK(nearest_result->parent_path() == child_dir);
  }

  fs::remove_all(temp_root);
}

TEST_CASE("manifest::discover nearest ignores root directive") {
  // Child has root=true, but nearest should still return it (first found)
  auto temp_root{ fs::canonical(fs::temp_directory_path()) /
                  "envy-test-nearest-root-true" };
  auto parent_dir{ temp_root / "parent" };
  auto child_dir{ parent_dir / "child" };

  fs::create_directories(child_dir);

  std::ofstream parent_manifest{ parent_dir / "envy.lua" };
  parent_manifest << "-- @envy bin \"tools\"\nPACKAGES = {}\n";
  parent_manifest.close();

  std::ofstream child_manifest{ child_dir / "envy.lua" };
  child_manifest << "-- @envy bin \"tools\"\n-- @envy root \"true\"\nPACKAGES = {}\n";
  child_manifest.close();

  {
    scoped_chdir cd{ child_dir };
    auto result{ envy::manifest::discover(true) };
    REQUIRE(result.has_value());
    CHECK(result->parent_path() == child_dir);
  }

  fs::remove_all(temp_root);
}

TEST_CASE("manifest::discover nearest from subdirectory without manifest") {
  // Start in a subdirectory that has no envy.lua; nearest should find parent's
  auto temp_root{ fs::canonical(fs::temp_directory_path()) / "envy-test-nearest-subdir" };
  auto parent_dir{ temp_root / "parent" };
  auto sub_dir{ parent_dir / "subdir" };

  fs::create_directories(sub_dir);

  std::ofstream parent_manifest{ parent_dir / "envy.lua" };
  parent_manifest << "-- @envy bin \"tools\"\n-- @envy root \"false\"\nPACKAGES = {}\n";
  parent_manifest.close();

  {
    scoped_chdir cd{ sub_dir };
    auto result{ envy::manifest::discover(true) };
    REQUIRE(result.has_value());
    CHECK(result->parent_path() == parent_dir);
  }

  fs::remove_all(temp_root);
}

// ============================================================================
// BUNDLES table tests
// ============================================================================

TEST_CASE("manifest::load parses package with bundle alias") {
  char const *script{ R"(
    -- @envy bin "tools"
    BUNDLES = {
      toolchain = {
        identity = "acme.toolchain@v1",
        source = "https://example.com/toolchain.tar.gz"
      }
    }
    PACKAGES = {
      {
        spec = "arm.gcc@v2",
        bundle = "toolchain"
      }
    }
  )" };

  auto m{ envy::manifest::load(script, fs::path("/fake/envy.lua")) };

  REQUIRE(m->packages.size() == 1);
  CHECK(m->packages[0]->identity == "arm.gcc@v2");
  CHECK(m->packages[0]->is_bundle_source());
  CHECK(m->packages[0]->bundle_identity.has_value());
  CHECK(*m->packages[0]->bundle_identity == "acme.toolchain@v1");

  auto const *bundle_src{ std::get_if<envy::pkg_cfg::bundle_source>(
      &m->packages[0]->source) };
  REQUIRE(bundle_src);
  CHECK(bundle_src->bundle_identity == "acme.toolchain@v1");
}

TEST_CASE("manifest::load parses package with inline bundle") {
  char const *script{ R"(
    -- @envy bin "tools"
    PACKAGES = {
      {
        spec = "arm.gcc@v2",
        bundle = {
          identity = "inline.bundle@v1",
          source = "https://example.com/inline.tar.gz"
        }
      }
    }
  )" };

  auto m{ envy::manifest::load(script, fs::path("/fake/envy.lua")) };

  REQUIRE(m->packages.size() == 1);
  CHECK(m->packages[0]->identity == "arm.gcc@v2");
  CHECK(m->packages[0]->is_bundle_source());
  CHECK(*m->packages[0]->bundle_identity == "inline.bundle@v1");
}

TEST_CASE("manifest::load errors on unknown bundle alias") {
  char const *script{ R"(
    -- @envy bin "tools"
    BUNDLES = {}
    PACKAGES = {
      {
        spec = "arm.gcc@v2",
        bundle = "nonexistent"
      }
    }
  )" };

  CHECK_THROWS_WITH_AS(envy::manifest::load(script, fs::path("/fake/envy.lua")),
                       "Bundle alias 'nonexistent' not found in BUNDLES table for spec "
                       "'arm.gcc@v2'",
                       std::runtime_error);
}

TEST_CASE("manifest::load errors on package with both source and bundle") {
  char const *script{ R"(
    -- @envy bin "tools"
    BUNDLES = {
      tc = { identity = "acme.tc@v1", source = "https://example.com/tc.tar.gz" }
    }
    PACKAGES = {
      {
        spec = "arm.gcc@v2",
        source = "https://example.com/gcc.lua",
        bundle = "tc"
      }
    }
  )" };

  CHECK_THROWS_WITH_AS(envy::manifest::load(script, fs::path("/fake/envy.lua")),
                       "Package cannot specify both 'source' and 'bundle' fields",
                       std::runtime_error);
}

TEST_CASE("manifest::load errors on bundle package without spec") {
  char const *script{ R"(
    -- @envy bin "tools"
    BUNDLES = {
      tc = { identity = "acme.tc@v1", source = "https://example.com/tc.tar.gz" }
    }
    PACKAGES = {
      {
        bundle = "tc"
      }
    }
  )" };

  CHECK_THROWS_WITH_AS(envy::manifest::load(script, fs::path("/fake/envy.lua")),
                       "Package with 'bundle' field requires 'spec' field",
                       std::runtime_error);
}

TEST_CASE("manifest::load parses package with bundle and options") {
  char const *script{ R"(
    -- @envy bin "tools"
    BUNDLES = {
      tc = { identity = "acme.tc@v1", source = "https://example.com/tc.tar.gz" }
    }
    PACKAGES = {
      {
        spec = "arm.gcc@v2",
        bundle = "tc",
        options = { version = "13.2.0" }
      }
    }
  )" };

  auto m{ envy::manifest::load(script, fs::path("/fake/envy.lua")) };

  REQUIRE(m->packages.size() == 1);
  CHECK(m->packages[0]->identity == "arm.gcc@v2");
  CHECK(m->packages[0]->is_bundle_source());

  // Check options are preserved
  sol::state lua;
  auto opts_result{ lua.safe_script("return " + m->packages[0]->serialized_options) };
  REQUIRE(opts_result.valid());
  sol::table opts = opts_result;
  CHECK(sol::object(opts["version"]).as<std::string>() == "13.2.0");
}
