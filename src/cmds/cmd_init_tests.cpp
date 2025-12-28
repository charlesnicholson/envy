#include "cmds/cmd_init.h"

#include "doctest.h"

#include <cstdlib>
#include <filesystem>

namespace fs = std::filesystem;

TEST_CASE("make_portable_path") {
#ifdef _WIN32
  char const *home{ std::getenv("USERPROFILE") };
  char const env_var[]{ "${env:USERPROFILE}" };
  char const sep{ '\\' };
#else
  char const *home{ std::getenv("HOME") };
  char const env_var[]{ "${env:HOME}" };
  char const sep{ '/' };
#endif
  REQUIRE(home);
  std::string const home_str{ home };

  SUBCASE("replaces home prefix with env var") {
    fs::path const path{ home_str + sep + "Library" + sep + "Caches" };
    CHECK(envy::make_portable_path(path) ==
          std::string{ env_var } + sep + "Library" + sep + "Caches");
  }

  SUBCASE("preserves paths not under home") {
#ifdef _WIN32
    fs::path const path{ "C:\\Windows\\System32" };
    CHECK(envy::make_portable_path(path) == "C:\\Windows\\System32");
#else
    fs::path const path{ "/tmp/some/other/path" };
    CHECK(envy::make_portable_path(path) == "/tmp/some/other/path");
#endif
  }

  SUBCASE("handles home as exact path") {
    fs::path const path{ home_str };
    CHECK(envy::make_portable_path(path) == env_var);
  }

  SUBCASE("does not replace partial home matches") {
    fs::path const path{ home_str + "-other" + sep + "something" };
    CHECK(envy::make_portable_path(path) == home_str + "-other" + sep + "something");
  }
}
