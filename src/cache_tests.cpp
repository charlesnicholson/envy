#include "cache.h"

#include "doctest.h"
#include "embedded_init_resources.h"
#include "platform.h"

#include <filesystem>
#include <fstream>
#include <random>

#ifndef ENVY_VERSION_STR
#error "ENVY_VERSION_STR must be defined by the build system"
#endif

namespace {

std::string make_entry_name() { return "foo.darwin-arm64-blake3-deadbeef"; }

}  // namespace

// Doctest fixture for tests that need a temporary cache directory
struct temp_cache_fixture {
  temp_cache_fixture() {
    static std::mt19937_64 rng{ std::random_device{}() };
    auto suffix = std::to_string(rng());
    temp_root = std::filesystem::temp_directory_path() /
                std::filesystem::path("envy-cache-test-unit-" + suffix);
    std::filesystem::create_directories(temp_root);
    cache = std::make_unique<envy::cache>(temp_root);
  }

  ~temp_cache_fixture() {
    cache.reset();  // Destroy cache before cleaning up directory
    std::error_code ec;
    std::filesystem::remove_all(temp_root, ec);
    if (ec) {
      std::string msg = "Failed to clean up temp cache directory '" + temp_root.string() +
                        "': " + ec.message();
      FAIL_CHECK(msg.c_str());
    }
  }

  std::filesystem::path temp_root;
  std::unique_ptr<envy::cache> cache;
};

TEST_CASE_FIXTURE(temp_cache_fixture, "repeated mark_install_complete calls are safe") {
  auto result = cache->ensure_pkg("foo", "darwin", "arm64", "deadbeef");
  REQUIRE(result.lock != nullptr);

  // Write something to install_dir
  auto install_file = result.lock->install_dir() / "output.txt";
  {
    std::ofstream ofs{ install_file };
    ofs << "installed";
  }  // Ensure file is closed before calling mark_install_complete

  // Call mark_install_complete once (multiple calls tested in destructor idempotency)
  result.lock->mark_install_complete();

  // Should complete successfully without error
  CHECK(true);

  // Explicitly destroy the lock before fixture destructor runs
  result.lock.reset();
}

TEST_CASE_FIXTURE(temp_cache_fixture, "cache root path") {
  CHECK(cache->root() == temp_root);
}

TEST_CASE("cache is_entry_complete") {
  // Complete entry has envy-complete marker
  CHECK(envy::cache::is_entry_complete("test_data/cache/complete-entry"));

  // Incomplete entry missing marker
  CHECK_FALSE(envy::cache::is_entry_complete("test_data/cache/incomplete-entry"));

  // Nonexistent entry
  CHECK_FALSE(envy::cache::is_entry_complete("test_data/cache/nonexistent"));
}

TEST_CASE("scoped_entry_lock is unmovable") {
  // Neither movable nor copyable (uses unique_ptr for transfer)
  CHECK_FALSE(std::is_move_constructible_v<envy::cache::scoped_entry_lock>);
  CHECK_FALSE(std::is_move_assignable_v<envy::cache::scoped_entry_lock>);
  CHECK_FALSE(std::is_copy_constructible_v<envy::cache::scoped_entry_lock>);
  CHECK_FALSE(std::is_copy_assignable_v<envy::cache::scoped_entry_lock>);
}

TEST_CASE_FIXTURE(
    temp_cache_fixture,
    "ensure_asset returns lock for cold entry and publishes asset directory") {
  auto result = cache->ensure_pkg("foo", "darwin", "arm64", "deadbeef");
  CHECK(result.lock != nullptr);
  CHECK_FALSE(result.pkg_path.empty());
  CHECK(std::filesystem::exists(result.lock->install_dir()));
  CHECK(std::filesystem::exists(result.lock->stage_dir()));
  CHECK(std::filesystem::exists(result.lock->fetch_dir()));

  // Simulate install: drop file into install dir then mark complete
  auto payload = result.lock->install_dir() / "sentinel.txt";
  std::ofstream{ payload } << "ok";
  result.lock->mark_install_complete();
  result.lock.reset();

  CHECK(std::filesystem::exists(result.entry_path / "envy-complete"));
  CHECK(std::filesystem::exists(result.pkg_path / "sentinel.txt"));
  CHECK_FALSE(std::filesystem::exists(result.entry_path / "work"));
}

TEST_CASE_FIXTURE(temp_cache_fixture, "ensure_asset fast path when marker present") {
  auto entry_dir = temp_root / "packages" / "foo" / "darwin-arm64-blake3-deadbeef";
  auto asset_dir = entry_dir / "pkg";
  std::filesystem::create_directories(asset_dir);
  std::ofstream{ asset_dir / "existing.txt" } << "cached";
  envy::platform::touch_file(entry_dir / "envy-complete");

  auto result = cache->ensure_pkg("foo", "darwin", "arm64", "deadbeef");
  CHECK(result.lock == nullptr);
  CHECK(result.pkg_path == asset_dir);
  CHECK(std::filesystem::exists(result.pkg_path / "existing.txt"));
}

TEST_CASE_FIXTURE(temp_cache_fixture, "mark_fetch_complete creates sentinel") {
  auto result = cache->ensure_pkg("foo", "darwin", "arm64", "deadbeef");
  REQUIRE(result.lock != nullptr);

  CHECK_FALSE(result.lock->is_fetch_complete());
  result.lock->mark_fetch_complete();
  CHECK(result.lock->is_fetch_complete());
  CHECK(std::filesystem::exists(result.lock->fetch_dir() / "envy-complete"));
  result.lock.reset();
}

TEST_CASE_FIXTURE(temp_cache_fixture, "fetch_dir preserved when marked complete") {
  // First acquisition: populate fetch/ and mark complete
  {
    auto result = cache->ensure_pkg("foo", "darwin", "arm64", "deadbeef");
    REQUIRE(result.lock != nullptr);

    auto fetch_file = result.lock->fetch_dir() / "payload.tar.gz";
    std::ofstream{ fetch_file } << "large download";
    result.lock->mark_fetch_complete();
  }

  // Second acquisition: verify fetch/ survived
  {
    auto result = cache->ensure_pkg("foo", "darwin", "arm64", "deadbeef");
    REQUIRE(result.lock != nullptr);

    CHECK(result.lock->is_fetch_complete());
    auto fetch_file = result.lock->fetch_dir() / "payload.tar.gz";
    CHECK(std::filesystem::exists(fetch_file));

    std::ifstream ifs{ fetch_file };
    std::string content{ std::istreambuf_iterator<char>{ ifs }, {} };
    CHECK(content == "large download");
  }
}

TEST_CASE_FIXTURE(temp_cache_fixture, "fetch_dir preserved when not marked complete") {
  // First acquisition: populate fetch/ but don't mark complete (simulates crash)
  {
    auto result = cache->ensure_pkg("foo", "darwin", "arm64", "deadbeef");
    REQUIRE(result.lock != nullptr);

    auto fetch_file = result.lock->fetch_dir() / "partial.tar.gz";
    std::ofstream{ fetch_file } << "incomplete";
    // Note: intentionally not calling mark_fetch_complete()
  }

  // Second acquisition: verify fetch/ was preserved for per-file caching
  {
    auto result = cache->ensure_pkg("foo", "darwin", "arm64", "deadbeef");
    REQUIRE(result.lock != nullptr);

    CHECK_FALSE(result.lock->is_fetch_complete());
    auto fetch_file = result.lock->fetch_dir() / "partial.tar.gz";
    CHECK(std::filesystem::exists(fetch_file));

    std::ifstream ifs{ fetch_file };
    std::string content{ std::istreambuf_iterator<char>{ ifs }, {} };
    CHECK(content == "incomplete");
  }
}

TEST_CASE_FIXTURE(
    temp_cache_fixture,
    "programmatic package with empty install_dir and fetch_dir cleans up cache") {
  auto const entry_name = make_entry_name();
  std::filesystem::path entry_dir;

  // Acquire lock, don't mark complete, leave directories empty
  {
    auto result = cache->ensure_pkg("foo", "darwin", "arm64", "deadbeef");
    REQUIRE(result.lock != nullptr);
    entry_dir = result.entry_path;

    // Don't call mark_install_complete()
    // install_dir and fetch_dir are empty (no files written)
  }
  // Lock destructor should purge entire cache entry

  // Verify cache entry directories were cleaned up
  CHECK_FALSE(std::filesystem::exists(entry_dir / "asset"));
  CHECK_FALSE(std::filesystem::exists(entry_dir / "fetch"));
  CHECK_FALSE(std::filesystem::exists(entry_dir / "install"));
  CHECK_FALSE(std::filesystem::exists(entry_dir / "work"));
}

TEST_CASE_FIXTURE(temp_cache_fixture, "programmatic package with fetch_dir preserved") {
  std::filesystem::path entry_dir;
  std::filesystem::path fetch_file;

  // First acquisition: populate fetch_dir but don't mark complete
  {
    auto result = cache->ensure_pkg("foo", "darwin", "arm64", "deadbeef");
    REQUIRE(result.lock != nullptr);
    entry_dir = result.entry_path;

    // Write file to fetch_dir
    fetch_file = result.lock->fetch_dir() / "downloaded.tar.gz";
    std::ofstream{ fetch_file } << "large payload";

    // Don't call mark_install_complete()
    // install_dir is empty, but fetch_dir has content
  }
  // Lock destructor should preserve fetch_dir

  // Verify fetch_dir was preserved
  CHECK(std::filesystem::exists(fetch_file));
  std::ifstream ifs{ fetch_file };
  std::string content{ std::istreambuf_iterator<char>{ ifs }, {} };
  CHECK(content == "large payload");

  // Verify other directories cleaned up
  CHECK_FALSE(std::filesystem::exists(entry_dir / "asset"));
  CHECK_FALSE(std::filesystem::exists(entry_dir / "work"));
}

TEST_CASE_FIXTURE(temp_cache_fixture,
                  "install_dir populated without mark_install_complete preserved") {
  std::filesystem::path entry_dir;

  // Acquire lock, populate install_dir, but don't mark complete
  {
    auto result = cache->ensure_pkg("foo", "darwin", "arm64", "deadbeef");
    REQUIRE(result.lock != nullptr);
    entry_dir = result.entry_path;

    // Write file to install_dir
    auto install_file = result.lock->install_dir() / "artifact.so";
    std::ofstream{ install_file } << "compiled binary";

    // Don't call mark_install_complete()
  }
  // Lock destructor cleans install_dir but not the entry

  // Verify install_dir was cleaned
  CHECK_FALSE(std::filesystem::exists(entry_dir / "install"));
  CHECK_FALSE(std::filesystem::exists(entry_dir / "work"));

  // Entry itself should still exist (for retry)
  CHECK(std::filesystem::exists(entry_dir));
}

TEST_CASE_FIXTURE(temp_cache_fixture,
                  "user-managed package with empty dirs purges entire entry_dir") {
  std::filesystem::path entry_dir;

  // Acquire lock, mark as user-managed, leave directories empty
  {
    auto result = cache->ensure_pkg("foo", "darwin", "arm64", "deadbeef");
    REQUIRE(result.lock != nullptr);
    entry_dir = result.entry_path;

    // Mark as user-managed (simulates check phase marking)
    result.lock->mark_user_managed();

    // Don't write any files, don't call mark_install_complete()
  }
  // Lock destructor should purge entire entry_dir

  // Verify entire entry_dir was deleted
  CHECK_FALSE(std::filesystem::exists(entry_dir));
}

TEST_CASE_FIXTURE(
    temp_cache_fixture,
    "user-managed package with fetch_dir populated purges entire entry_dir") {
  std::filesystem::path entry_dir;
  std::filesystem::path fetch_file;

  // Acquire lock, mark as user-managed, populate fetch_dir
  {
    auto result = cache->ensure_pkg("foo", "darwin", "arm64", "deadbeef");
    REQUIRE(result.lock != nullptr);
    entry_dir = result.entry_path;

    // Write file to fetch_dir
    fetch_file = result.lock->fetch_dir() / "downloaded.tar.gz";
    std::ofstream{ fetch_file } << "large payload";

    // Mark as user-managed
    result.lock->mark_user_managed();

    // Don't call mark_install_complete()
  }
  // Lock destructor should purge entire entry_dir (including fetch_dir)

  // Verify entire entry_dir was deleted (including fetch_dir with contents)
  CHECK_FALSE(std::filesystem::exists(entry_dir));
  CHECK_FALSE(std::filesystem::exists(fetch_file));
}

TEST_CASE_FIXTURE(
    temp_cache_fixture,
    "user-managed package with install_dir populated purges entire entry_dir") {
  std::filesystem::path entry_dir;

  // Acquire lock, mark as user-managed, populate install_dir
  {
    auto result = cache->ensure_pkg("foo", "darwin", "arm64", "deadbeef");
    REQUIRE(result.lock != nullptr);
    entry_dir = result.entry_path;

    // Write file to install_dir
    auto install_file = result.lock->install_dir() / "artifact.so";
    std::ofstream{ install_file } << "compiled binary";

    // Mark as user-managed
    result.lock->mark_user_managed();

    // Don't call mark_install_complete()
  }
  // Lock destructor should purge entire entry_dir (including install_dir)

  // Verify entire entry_dir was deleted
  CHECK_FALSE(std::filesystem::exists(entry_dir));
}

TEST_CASE_FIXTURE(temp_cache_fixture,
                  "user-managed package with all dirs populated purges entire entry_dir") {
  std::filesystem::path entry_dir;

  // Acquire lock, mark as user-managed, populate fetch/stage/install dirs
  {
    auto result = cache->ensure_pkg("foo", "darwin", "arm64", "deadbeef");
    REQUIRE(result.lock != nullptr);
    entry_dir = result.entry_path;

    // Write files to all workspace directories
    std::ofstream{ result.lock->fetch_dir() / "downloaded.tar.gz" } << "fetch payload";
    std::ofstream{ result.lock->stage_dir() / "extracted.txt" } << "stage payload";
    std::ofstream{ result.lock->install_dir() / "artifact.so" } << "install payload";

    // Mark as user-managed
    result.lock->mark_user_managed();

    // Don't call mark_install_complete()
  }
  // Lock destructor should purge entire entry_dir (all subdirectories)

  // Verify entire entry_dir was deleted
  CHECK_FALSE(std::filesystem::exists(entry_dir));
}

TEST_CASE_FIXTURE(temp_cache_fixture,
                  "lock file deletion attempted on successful completion") {
  std::filesystem::path lock_path;

  {
    auto result = cache->ensure_pkg("foo", "darwin", "arm64", "deadbeef");
    REQUIRE(result.lock != nullptr);

    // Capture lock path (not exposed via public API, but we can infer it)
    lock_path = temp_root / "locks" / "packages.foo.darwin-arm64-blake3-deadbeef.lock";

    // Verify lock file exists while lock is held
    CHECK(std::filesystem::exists(lock_path));

    // Write to install_dir and mark complete
    std::ofstream{ result.lock->install_dir() / "output.txt" } << "done";
    result.lock->mark_install_complete();
  }
  // Lock destructor attempts to delete lock file

  // Lock file should be deleted (no other process holding it in this test)
  CHECK_FALSE(std::filesystem::exists(lock_path));
}

TEST_CASE_FIXTURE(temp_cache_fixture,
                  "lock file deletion attempted on user-managed purge") {
  std::filesystem::path lock_path;

  {
    auto result = cache->ensure_pkg("foo", "darwin", "arm64", "deadbeef");
    REQUIRE(result.lock != nullptr);

    lock_path = temp_root / "locks" / "packages.foo.darwin-arm64-blake3-deadbeef.lock";

    // Verify lock file exists while lock is held
    CHECK(std::filesystem::exists(lock_path));

    // Mark as user-managed
    result.lock->mark_user_managed();
  }
  // Lock destructor attempts to delete lock file

  // Lock file should be deleted
  CHECK_FALSE(std::filesystem::exists(lock_path));
}

TEST_CASE_FIXTURE(temp_cache_fixture,
                  "user-managed complete cleanup: entry_dir and lock file both deleted") {
  std::filesystem::path entry_dir;
  std::filesystem::path lock_path;

  {
    auto result = cache->ensure_pkg("foo", "darwin", "arm64", "deadbeef");
    REQUIRE(result.lock != nullptr);

    entry_dir = result.entry_path;
    lock_path = temp_root / "locks" / "packages.foo.darwin-arm64-blake3-deadbeef.lock";

    // Verify both exist while lock is held
    CHECK(std::filesystem::exists(entry_dir));
    CHECK(std::filesystem::exists(lock_path));

    // Populate all directories to ensure they get cleaned up
    std::ofstream{ result.lock->fetch_dir() / "download.tar.gz" } << "fetch data";
    std::ofstream{ result.lock->stage_dir() / "staged.txt" } << "stage data";
    std::ofstream{ result.lock->install_dir() / "binary.so" } << "install data";

    // Verify subdirectories exist
    CHECK(std::filesystem::exists(result.lock->fetch_dir() / "download.tar.gz"));
    CHECK(std::filesystem::exists(result.lock->stage_dir() / "staged.txt"));
    CHECK(std::filesystem::exists(result.lock->install_dir() / "binary.so"));

    // Mark as user-managed
    result.lock->mark_user_managed();
  }
  // scoped_entry_lock destructor runs first: purges entry_dir
  // Then file_lock destructor runs: deletes lock file

  // Verify complete cleanup: both entry_dir and lock file deleted
  CHECK_FALSE(std::filesystem::exists(entry_dir));
  CHECK_FALSE(std::filesystem::exists(entry_dir / "fetch"));
  CHECK_FALSE(std::filesystem::exists(entry_dir / "stage"));
  CHECK_FALSE(std::filesystem::exists(entry_dir / "install"));
  CHECK_FALSE(std::filesystem::exists(entry_dir / "work"));
  CHECK_FALSE(std::filesystem::exists(lock_path));
}

TEST_CASE_FIXTURE(
    temp_cache_fixture,
    "cache-managed success: entry_dir preserved with asset, temp dirs cleaned") {
  std::filesystem::path entry_dir;
  std::filesystem::path asset_dir;
  std::filesystem::path lock_path;

  {
    auto result = cache->ensure_pkg("foo", "darwin", "arm64", "deadbeef");
    REQUIRE(result.lock != nullptr);

    entry_dir = result.entry_path;
    asset_dir = result.pkg_path;
    lock_path = temp_root / "locks" / "packages.foo.darwin-arm64-blake3-deadbeef.lock";

    // Verify entry_dir and lock file exist while lock is held
    CHECK(std::filesystem::exists(entry_dir));
    CHECK(std::filesystem::exists(lock_path));

    // Populate all directories
    std::ofstream{ result.lock->fetch_dir() / "download.tar.gz" } << "fetch data";
    std::ofstream{ result.lock->stage_dir() / "staged.txt" } << "stage data";
    std::ofstream{ result.lock->install_dir() / "binary.so" } << "install data";
    std::ofstream{ result.lock->install_dir() / "library.a" } << "library data";

    // Mark as successfully installed (NOT user-managed)
    result.lock->mark_install_complete();
  }
  // scoped_entry_lock destructor runs success path: rename install -> asset
  // Then file_lock destructor runs: deletes lock file

  // Verify entry_dir is preserved
  CHECK(std::filesystem::exists(entry_dir));

  // Verify asset/ dir exists with installed files
  CHECK(std::filesystem::exists(asset_dir));
  CHECK(std::filesystem::exists(asset_dir / "binary.so"));
  CHECK(std::filesystem::exists(asset_dir / "library.a"));

  // Verify envy-complete marker created
  CHECK(std::filesystem::exists(entry_dir / "envy-complete"));

  // Verify temporary directories cleaned up
  CHECK_FALSE(std::filesystem::exists(entry_dir / "fetch"));
  CHECK_FALSE(std::filesystem::exists(entry_dir / "install"));
  CHECK_FALSE(std::filesystem::exists(entry_dir / "work"));

  // Verify lock file deleted
  CHECK_FALSE(std::filesystem::exists(lock_path));
}

// Tests for resolve_cache_root()

TEST_CASE("resolve_cache_root CLI override takes precedence") {
  std::optional<std::filesystem::path> cli{ "/cli/override/path" };
  std::optional<std::string> manifest{ "~/manifest/path" };

  auto result{ envy::resolve_cache_root(cli, manifest) };
  CHECK(result == "/cli/override/path");
}

TEST_CASE("resolve_cache_root manifest used when no CLI override") {
  // This test uses expand_path internally, so test with a plain path
  std::optional<std::filesystem::path> cli{ std::nullopt };
  std::optional<std::string> manifest{ "/manifest/path" };

  // Only run if ENVY_CACHE_ROOT is not set
  char const *orig_env{ std::getenv("ENVY_CACHE_ROOT") };
  if (orig_env) { return; }

  auto result{ envy::resolve_cache_root(cli, manifest) };
  CHECK(result == "/manifest/path");
}

#ifndef _WIN32
TEST_CASE("resolve_cache_root manifest with tilde is expanded") {
  char const *home{ std::getenv("HOME") };
  REQUIRE(home != nullptr);

  // Only run if ENVY_CACHE_ROOT is not set
  char const *orig_env{ std::getenv("ENVY_CACHE_ROOT") };
  if (orig_env) { return; }

  std::optional<std::filesystem::path> cli{ std::nullopt };
  std::optional<std::string> manifest{ "~/.my-envy-cache" };

  auto result{ envy::resolve_cache_root(cli, manifest) };
  CHECK(result == std::filesystem::path{ home } / ".my-envy-cache");
}
#endif

// Tests for ensure_envy()

TEST_CASE_FIXTURE(temp_cache_fixture, "ensure_envy deploys binary and types to cache") {
  std::string_view const types{ reinterpret_cast<char const *>(
                                    envy::embedded::kTypeDefinitions),
                                envy::embedded::kTypeDefinitionsSize };

  auto const envy_dir{
    cache->ensure_envy(ENVY_VERSION_STR, envy::platform::get_exe_path(), types)
  };

  CHECK(std::filesystem::exists(envy_dir));

#ifdef _WIN32
  CHECK(std::filesystem::exists(envy_dir / "envy.exe"));
#else
  CHECK(std::filesystem::exists(envy_dir / "envy"));
#endif

  CHECK(std::filesystem::exists(envy_dir / "envy.lua"));
}

TEST_CASE_FIXTURE(temp_cache_fixture, "ensure_envy is idempotent") {
  std::string_view const types{ reinterpret_cast<char const *>(
                                    envy::embedded::kTypeDefinitions),
                                envy::embedded::kTypeDefinitionsSize };
  auto const exe{ envy::platform::get_exe_path() };

  auto const dir1{ cache->ensure_envy(ENVY_VERSION_STR, exe, types) };
  auto const dir2{ cache->ensure_envy(ENVY_VERSION_STR, exe, types) };

  CHECK(dir1 == dir2);
}
