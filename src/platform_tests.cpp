#include "platform.h"

#include "doctest.h"

#include <cstdlib>
#include <filesystem>

namespace envy {

TEST_CASE("platform::get_exe_path returns valid path") {
  auto const path{ platform::get_exe_path() };

  CHECK(!path.empty());
  CHECK(path.is_absolute());
  CHECK(std::filesystem::exists(path));
  CHECK(std::filesystem::is_regular_file(path));
}

TEST_CASE("platform::get_exe_path returns executable file") {
  auto const path{ platform::get_exe_path() };
  auto const filename{ path.filename().string() };

  // Should be one of our test executables
  CHECK((filename.find("envy") != std::string::npos ||
         filename.find("test") != std::string::npos));
}

TEST_CASE("platform::expand_path empty path returns empty path") {
  auto result{ platform::expand_path("") };
  CHECK(result.empty());
}

TEST_CASE("platform::expand_path plain path returns unchanged") {
  auto result{ platform::expand_path("/absolute/path/to/something") };
  CHECK(result == "/absolute/path/to/something");
}

TEST_CASE("platform::expand_path relative path returns unchanged") {
  auto result{ platform::expand_path("relative/path") };
  CHECK(result == "relative/path");
}

#ifndef _WIN32
TEST_CASE("platform::expand_path tilde expands to HOME") {
  char const *home{ std::getenv("HOME") };
  REQUIRE(home != nullptr);  // HOME should always be set on Unix

  auto result{ platform::expand_path("~") };
  CHECK(result == std::filesystem::path{ home });
}

TEST_CASE("platform::expand_path tilde slash expands correctly") {
  char const *home{ std::getenv("HOME") };
  REQUIRE(home != nullptr);

  auto result{ platform::expand_path("~/foo/bar") };
  CHECK(result == std::filesystem::path{ home } / "foo" / "bar");
}

TEST_CASE("platform::expand_path with env var expands correctly") {
  char const *home{ std::getenv("HOME") };
  REQUIRE(home != nullptr);

  auto result{ platform::expand_path("$HOME/test") };
  CHECK(result == std::filesystem::path{ home } / "test");
}

TEST_CASE("platform::expand_path with braced env var expands correctly") {
  char const *home{ std::getenv("HOME") };
  REQUIRE(home != nullptr);

  auto result{ platform::expand_path("${HOME}/test") };
  CHECK(result == std::filesystem::path{ home } / "test");
}
#endif

}  // namespace envy
