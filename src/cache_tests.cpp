#include "cache.h"

#include "doctest.h"

TEST_CASE("cache root path") {
  envy::cache c{ "/tmp/test-cache" };
  CHECK(c.root() == "/tmp/test-cache");
}

TEST_CASE("cache is_entry_complete") {
  // Complete entry has .envy-complete marker
  CHECK(envy::cache::is_entry_complete("test_data/cache/complete-entry"));

  // Incomplete entry missing marker
  CHECK_FALSE(envy::cache::is_entry_complete("test_data/cache/incomplete-entry"));

  // Nonexistent entry
  CHECK_FALSE(envy::cache::is_entry_complete("test_data/cache/nonexistent"));
}

TEST_CASE("scoped_entry_lock move semantics") {
  // Moveable but not copyable
  CHECK(std::is_move_constructible_v<envy::cache::scoped_entry_lock>);
  CHECK(std::is_move_assignable_v<envy::cache::scoped_entry_lock>);
  CHECK(std::is_nothrow_move_constructible_v<envy::cache::scoped_entry_lock>);
  CHECK(std::is_nothrow_move_assignable_v<envy::cache::scoped_entry_lock>);
  CHECK_FALSE(std::is_copy_constructible_v<envy::cache::scoped_entry_lock>);
  CHECK_FALSE(std::is_copy_assignable_v<envy::cache::scoped_entry_lock>);
}
