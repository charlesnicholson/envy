#include "shell.h"
#include "doctest.h"
#include <string>
#include <unordered_map>

TEST_CASE("shell_getenv captures PATH") {
  auto env{ envy::shell_getenv() };
  REQUIRE(!env.empty());
#if defined(_WIN32)
  bool has_path_variant = (env.find("Path") != env.end()) || (env.find("PATH") != env.end());
  CHECK(has_path_variant);
#else
  CHECK(env.find("PATH") != env.end());
#endif
}
