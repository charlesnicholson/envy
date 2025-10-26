#include "cache.h"

#include "doctest.h"

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

std::string make_entry_name() { return "foo.darwin-arm64-sha256-deadbeef"; }

}  // namespace

TEST_CASE("cache root path") {
  auto root = make_temp_root();
  envy::cache c{ root };
  CHECK(c.root() == root);
  std::filesystem::remove_all(root);
}

TEST_CASE("cache is_entry_complete") {
  // Complete entry has .envy-complete marker
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
  result.lock->mark_complete();
  result.lock.reset();

  CHECK(std::filesystem::exists(result.entry_path / ".envy-complete"));
  CHECK(std::filesystem::exists(result.asset_path / "sentinel.txt"));
  CHECK_FALSE(std::filesystem::exists(result.entry_path / ".work"));

  std::filesystem::remove_all(root);
}

TEST_CASE("ensure_asset fast path when marker present") {
  auto root = make_temp_root();
  envy::cache c{ root };

  auto entry_dir = root / "assets" / "foo.darwin-arm64-sha256-deadbeef";
  auto asset_dir = entry_dir / "asset";
  std::filesystem::create_directories(asset_dir);
  std::ofstream{ asset_dir / "existing.txt" } << "cached";
  std::ofstream{ entry_dir / ".envy-complete" }.close();

  auto result = c.ensure_asset("foo", "darwin", "arm64", "deadbeef");
  CHECK(result.lock == nullptr);
  CHECK(result.asset_path == asset_dir);
  CHECK(std::filesystem::exists(result.asset_path / "existing.txt"));

  std::filesystem::remove_all(root);
}
