#include "bootstrap.h"

#include "platform.h"
#include "tui.h"

#include "doctest.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace {

struct temp_dir_with_env {
  fs::path path;
  std::string original_cache_root;
  bool had_original;

  temp_dir_with_env() : path{ fs::temp_directory_path() / "envy_bootstrap_test" } {
    // Suppress tui output to avoid polluting other tests
    envy::tui::set_output_handler([](std::string_view) {});

    // Save original ENVY_CACHE_ROOT
    char const *orig{ std::getenv("ENVY_CACHE_ROOT") };
    had_original = orig != nullptr;
    if (had_original) { original_cache_root = orig; }

    fs::remove_all(path);
    fs::create_directories(path);

    // Set ENVY_CACHE_ROOT to our temp directory
    envy::platform::set_env_var("ENVY_CACHE_ROOT", path.string().c_str());
  }

  ~temp_dir_with_env() {
    // Restore original ENVY_CACHE_ROOT
    if (had_original) {
      envy::platform::set_env_var("ENVY_CACHE_ROOT", original_cache_root.c_str());
    } else {
      // Can't unset, but set to empty string
      envy::platform::set_env_var("ENVY_CACHE_ROOT", "");
    }
    fs::remove_all(path);
  }

  temp_dir_with_env(temp_dir_with_env const &) = delete;
  temp_dir_with_env &operator=(temp_dir_with_env const &) = delete;
};

struct temp_dir {
  fs::path path;

  temp_dir() : path{ fs::temp_directory_path() / "envy_bootstrap_test" } {
    // Suppress tui output to avoid polluting other tests
    envy::tui::set_output_handler([](std::string_view) {});

    fs::remove_all(path);
    fs::create_directories(path);
  }

  ~temp_dir() { fs::remove_all(path); }

  temp_dir(temp_dir const &) = delete;
  temp_dir &operator=(temp_dir const &) = delete;
};

}  // namespace

TEST_CASE("bootstrap_extract_lua_ls_types creates type definitions file") {
  temp_dir_with_env tmp;

  fs::path const types_dir{ envy::bootstrap_extract_lua_ls_types() };

  CHECK(fs::exists(types_dir));
  CHECK(fs::exists(types_dir / "envy.lua"));

  // Verify the file has content
  std::ifstream in{ types_dir / "envy.lua" };
  REQUIRE(in.good());
  std::string content{ std::istreambuf_iterator<char>(in), {} };
  CHECK(!content.empty());
  CHECK(content.find("envy") != std::string::npos);
}

TEST_CASE("bootstrap_extract_lua_ls_types is idempotent") {
  temp_dir_with_env tmp;

  fs::path const types_dir1{ envy::bootstrap_extract_lua_ls_types() };
  fs::path const types_dir2{ envy::bootstrap_extract_lua_ls_types() };

  CHECK(types_dir1 == types_dir2);
  CHECK(fs::exists(types_dir2 / "envy.lua"));
}

TEST_CASE("bootstrap_deploy_envy deploys binary and types to cache") {
  temp_dir tmp;

  envy::cache c{ tmp.path };

  bool const result{ envy::bootstrap_deploy_envy(c) };
  CHECK(result);

  // The envy directory should exist after deployment
  // Note: The exact path depends on ENVY_VERSION_STR
  bool found_envy_dir{ false };
  for (auto const &entry : fs::directory_iterator(tmp.path / "envy")) {
    if (entry.is_directory()) {
      // Check for binary
#ifdef _WIN32
      found_envy_dir = fs::exists(entry.path() / "envy.exe");
#else
      found_envy_dir = fs::exists(entry.path() / "envy");
#endif
      if (found_envy_dir) {
        // Also check for type definitions
        CHECK(fs::exists(entry.path() / "envy.lua"));
        break;
      }
    }
  }
  CHECK(found_envy_dir);
}

TEST_CASE("bootstrap_deploy_envy is idempotent") {
  temp_dir tmp;

  envy::cache c{ tmp.path };

  bool const result1{ envy::bootstrap_deploy_envy(c) };
  bool const result2{ envy::bootstrap_deploy_envy(c) };

  CHECK(result1);
  CHECK(result2);
}
