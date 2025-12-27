#include "platform.h"

#include "doctest.h"

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

}  // namespace envy
