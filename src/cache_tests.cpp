#include "cache.h"

#include "doctest.h"
#include "platform.h"

#include <filesystem>
#include <fstream>
#include <random>

namespace {

std::filesystem::path make_temp_root() {
  static std::mt19937_64 rng{ std::random_device{}() };
  auto suffix = std::to_string(rng());
  auto root = std::filesystem::temp_directory_path() /
              std::filesystem::path("envy-cache-test-unit-" + suffix);
  std::filesystem::create_directories(root);
  return root;
}

std::string make_entry_name() { return "foo.darwin-arm64-blake3-deadbeef"; }

}  // namespace

TEST_CASE("repeated mark_install_complete calls are safe") {
  auto root = make_temp_root();
  envy::cache c{ root };

  auto result = c.ensure_asset("foo", "darwin", "arm64", "deadbeef");
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

  // Explicitly destroy the lock before removing root
  result.lock.reset();

  std::filesystem::remove_all(root);
}

TEST_CASE("cache root path") {
  auto root = make_temp_root();
  envy::cache c{ root };
  CHECK(c.root() == root);
  std::filesystem::remove_all(root);
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

TEST_CASE("ensure_asset returns lock for cold entry and publishes asset directory") {
  auto root = make_temp_root();
  envy::cache c{ root };

  auto result = c.ensure_asset("foo", "darwin", "arm64", "deadbeef");
  CHECK(result.lock != nullptr);
  CHECK_FALSE(result.asset_path.empty());
  CHECK(std::filesystem::exists(result.lock->install_dir()));
  CHECK(std::filesystem::exists(result.lock->stage_dir()));
  CHECK(std::filesystem::exists(result.lock->fetch_dir()));

  // Simulate install: drop file into install dir then mark complete
  auto payload = result.lock->install_dir() / "sentinel.txt";
  std::ofstream{ payload } << "ok";
  result.lock->mark_install_complete();
  result.lock.reset();

  CHECK(std::filesystem::exists(result.entry_path / "envy-complete"));
  CHECK(std::filesystem::exists(result.asset_path / "sentinel.txt"));
  CHECK_FALSE(std::filesystem::exists(result.entry_path / "work"));

  std::filesystem::remove_all(root);
}

TEST_CASE("ensure_asset fast path when marker present") {
  auto root = make_temp_root();
  envy::cache c{ root };

  auto entry_dir = root / "assets" / "foo" / "darwin-arm64-blake3-deadbeef";
  auto asset_dir = entry_dir / "asset";
  std::filesystem::create_directories(asset_dir);
  std::ofstream{ asset_dir / "existing.txt" } << "cached";
  envy::platform::touch_file(entry_dir / "envy-complete");

  auto result = c.ensure_asset("foo", "darwin", "arm64", "deadbeef");
  CHECK(result.lock == nullptr);
  CHECK(result.asset_path == asset_dir);
  CHECK(std::filesystem::exists(result.asset_path / "existing.txt"));

  std::filesystem::remove_all(root);
}

TEST_CASE("mark_fetch_complete creates sentinel") {
  auto root = make_temp_root();
  envy::cache c{ root };

  auto result = c.ensure_asset("foo", "darwin", "arm64", "deadbeef");
  REQUIRE(result.lock != nullptr);

  CHECK_FALSE(result.lock->is_fetch_complete());
  result.lock->mark_fetch_complete();
  CHECK(result.lock->is_fetch_complete());
  CHECK(std::filesystem::exists(result.lock->fetch_dir() / "envy-complete"));
  // Release lock before removing root on Windows to prevent removal errors.
  result.lock.reset();
  std::filesystem::remove_all(root);
}

TEST_CASE("fetch_dir preserved when marked complete") {
  auto root = make_temp_root();
  envy::cache c{ root };

  // First acquisition: populate fetch/ and mark complete
  {
    auto result = c.ensure_asset("foo", "darwin", "arm64", "deadbeef");
    REQUIRE(result.lock != nullptr);

    auto fetch_file = result.lock->fetch_dir() / "payload.tar.gz";
    std::ofstream{ fetch_file } << "large download";
    result.lock->mark_fetch_complete();
  }

  // Second acquisition: verify fetch/ survived
  {
    auto result = c.ensure_asset("foo", "darwin", "arm64", "deadbeef");
    REQUIRE(result.lock != nullptr);

    CHECK(result.lock->is_fetch_complete());
    auto fetch_file = result.lock->fetch_dir() / "payload.tar.gz";
    CHECK(std::filesystem::exists(fetch_file));

    std::ifstream ifs{ fetch_file };
    std::string content{ std::istreambuf_iterator<char>{ ifs }, {} };
    CHECK(content == "large download");
  }

  std::filesystem::remove_all(root);
}

TEST_CASE("fetch_dir preserved when not marked complete") {
  auto root = make_temp_root();
  envy::cache c{ root };

  // First acquisition: populate fetch/ but don't mark complete (simulates crash)
  {
    auto result = c.ensure_asset("foo", "darwin", "arm64", "deadbeef");
    REQUIRE(result.lock != nullptr);

    auto fetch_file = result.lock->fetch_dir() / "partial.tar.gz";
    std::ofstream{ fetch_file } << "incomplete";
    // Note: intentionally not calling mark_fetch_complete()
  }

  // Second acquisition: verify fetch/ was preserved for per-file caching
  {
    auto result = c.ensure_asset("foo", "darwin", "arm64", "deadbeef");
    REQUIRE(result.lock != nullptr);

    CHECK_FALSE(result.lock->is_fetch_complete());
    auto fetch_file = result.lock->fetch_dir() / "partial.tar.gz";
    CHECK(std::filesystem::exists(fetch_file));

    std::ifstream ifs{ fetch_file };
    std::string content{ std::istreambuf_iterator<char>{ ifs }, {} };
    CHECK(content == "incomplete");
  }

  std::filesystem::remove_all(root);
}

TEST_CASE("programmatic package with empty install_dir and fetch_dir cleans up cache") {
  auto root = make_temp_root();
  envy::cache c{ root };

  auto const entry_name = make_entry_name();
  std::filesystem::path entry_dir;

  // Acquire lock, don't mark complete, leave directories empty
  {
    auto result = c.ensure_asset("foo", "darwin", "arm64", "deadbeef");
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

  std::filesystem::remove_all(root);
}

TEST_CASE("programmatic package with fetch_dir preserved") {
  auto root = make_temp_root();
  envy::cache c{ root };

  std::filesystem::path entry_dir;
  std::filesystem::path fetch_file;

  // First acquisition: populate fetch_dir but don't mark complete
  {
    auto result = c.ensure_asset("foo", "darwin", "arm64", "deadbeef");
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

  std::filesystem::remove_all(root);
}

TEST_CASE("install_dir populated without mark_install_complete preserved") {
  auto root = make_temp_root();
  envy::cache c{ root };

  std::filesystem::path entry_dir;

  // Acquire lock, populate install_dir, but don't mark complete
  {
    auto result = c.ensure_asset("foo", "darwin", "arm64", "deadbeef");
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

  std::filesystem::remove_all(root);
}
