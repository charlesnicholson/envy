#include "package_depot.h"

#include "doctest.h"

#include <vector>

using namespace envy;

TEST_CASE("package_depot_index: empty index") {
  auto index{package_depot_index::build_from_contents({})};
  CHECK(index.empty());
  CHECK_FALSE(index.find("pkg@v1", "darwin", "arm64", "abcdef01").has_value());
}

TEST_CASE("package_depot_index: single manifest with one entry") {
  auto index{package_depot_index::build_from_contents(
      {"https://cdn.example.com/arm.gcc@r2-darwin-arm64-blake3-abcdef0123456789.tar.zst\n"})};

  CHECK_FALSE(index.empty());

  auto result{index.find("arm.gcc@r2", "darwin", "arm64", "abcdef0123456789")};
  REQUIRE(result.has_value());
  CHECK(*result ==
        "https://cdn.example.com/arm.gcc@r2-darwin-arm64-blake3-abcdef0123456789.tar.zst");
}

TEST_CASE("package_depot_index: miss on wrong identity") {
  auto index{package_depot_index::build_from_contents(
      {"https://cdn.example.com/arm.gcc@r2-darwin-arm64-blake3-abcdef0123456789.tar.zst\n"})};

  CHECK_FALSE(index.find("arm.gcc@r3", "darwin", "arm64", "abcdef0123456789").has_value());
}

TEST_CASE("package_depot_index: miss on wrong platform") {
  auto index{package_depot_index::build_from_contents(
      {"https://cdn.example.com/arm.gcc@r2-darwin-arm64-blake3-abcdef0123456789.tar.zst\n"})};

  CHECK_FALSE(index.find("arm.gcc@r2", "linux", "arm64", "abcdef0123456789").has_value());
}

TEST_CASE("package_depot_index: miss on wrong hash") {
  auto index{package_depot_index::build_from_contents(
      {"https://cdn.example.com/arm.gcc@r2-darwin-arm64-blake3-abcdef0123456789.tar.zst\n"})};

  CHECK_FALSE(index.find("arm.gcc@r2", "darwin", "arm64", "0000000000000000").has_value());
}

TEST_CASE("package_depot_index: multiple entries in one manifest") {
  auto index{package_depot_index::build_from_contents({
      "s3://bucket/arm.gcc@r2-darwin-arm64-blake3-aaaa.tar.zst\n"
      "s3://bucket/local.uv@r0-linux-x86_64-blake3-bbbb.tar.zst\n"})};

  auto r1{index.find("arm.gcc@r2", "darwin", "arm64", "aaaa")};
  REQUIRE(r1.has_value());
  CHECK(*r1 == "s3://bucket/arm.gcc@r2-darwin-arm64-blake3-aaaa.tar.zst");

  auto r2{index.find("local.uv@r0", "linux", "x86_64", "bbbb")};
  REQUIRE(r2.has_value());
  CHECK(*r2 == "s3://bucket/local.uv@r0-linux-x86_64-blake3-bbbb.tar.zst");
}

TEST_CASE("package_depot_index: blank lines and comments ignored") {
  auto index{package_depot_index::build_from_contents({
      "# This is a comment\n"
      "\n"
      "s3://bucket/arm.gcc@r2-darwin-arm64-blake3-aaaa.tar.zst\n"
      "\n"
      "# Another comment\n"
      "s3://bucket/local.uv@r0-linux-x86_64-blake3-bbbb.tar.zst\n"})};

  CHECK(index.find("arm.gcc@r2", "darwin", "arm64", "aaaa").has_value());
  CHECK(index.find("local.uv@r0", "linux", "x86_64", "bbbb").has_value());
}

TEST_CASE("package_depot_index: invalid lines skipped") {
  auto index{package_depot_index::build_from_contents({
      "garbage-nonsense\n"
      "s3://bucket/arm.gcc@r2-darwin-arm64-blake3-aaaa.tar.zst\n"
      "not-a-valid-archive.txt\n"})};

  CHECK(index.find("arm.gcc@r2", "darwin", "arm64", "aaaa").has_value());
}

TEST_CASE("package_depot_index: multiple manifests searched in order") {
  auto index{package_depot_index::build_from_contents({
      "https://depot1/arm.gcc@r2-darwin-arm64-blake3-aaaa.tar.zst\n",
      "https://depot2/arm.gcc@r2-darwin-arm64-blake3-aaaa.tar.zst\n"})};

  // First manifest wins
  auto result{index.find("arm.gcc@r2", "darwin", "arm64", "aaaa")};
  REQUIRE(result.has_value());
  CHECK(*result == "https://depot1/arm.gcc@r2-darwin-arm64-blake3-aaaa.tar.zst");
}

TEST_CASE("package_depot_index: disjoint manifests both consulted") {
  auto index{package_depot_index::build_from_contents({
      "https://depot1/arm.gcc@r2-darwin-arm64-blake3-aaaa.tar.zst\n",
      "https://depot2/local.uv@r0-linux-x86_64-blake3-bbbb.tar.zst\n"})};

  CHECK(index.find("arm.gcc@r2", "darwin", "arm64", "aaaa").has_value());
  CHECK(index.find("local.uv@r0", "linux", "x86_64", "bbbb").has_value());
}

TEST_CASE("package_depot_index: first manifest with match stops search") {
  // Manifest 1 has pkg A, manifest 2 has pkg A (different URL) and pkg B
  auto index{package_depot_index::build_from_contents({
      "https://depot1/arm.gcc@r2-darwin-arm64-blake3-aaaa.tar.zst\n",
      "https://depot2/arm.gcc@r2-darwin-arm64-blake3-aaaa.tar.zst\n"
      "https://depot2/local.uv@r0-linux-x86_64-blake3-bbbb.tar.zst\n"})};

  // pkg A found in first manifest
  auto r1{index.find("arm.gcc@r2", "darwin", "arm64", "aaaa")};
  REQUIRE(r1.has_value());
  CHECK(*r1 == "https://depot1/arm.gcc@r2-darwin-arm64-blake3-aaaa.tar.zst");

  // pkg B only in second manifest — still found
  auto r2{index.find("local.uv@r0", "linux", "x86_64", "bbbb")};
  REQUIRE(r2.has_value());
  CHECK(*r2 == "https://depot2/local.uv@r0-linux-x86_64-blake3-bbbb.tar.zst");
}

TEST_CASE("package_depot_index: empty manifest text yields empty index") {
  auto index{package_depot_index::build_from_contents({"", "\n\n"})};
  CHECK(index.empty());
}

TEST_CASE("package_depot_index: S3 URLs parsed correctly") {
  auto index{package_depot_index::build_from_contents({
      "s3://my-bucket/cache/arm.gcc@r2-darwin-arm64-blake3-abcdef0123456789.tar.zst\n"})};

  auto result{index.find("arm.gcc@r2", "darwin", "arm64", "abcdef0123456789")};
  REQUIRE(result.has_value());
  CHECK(*result ==
        "s3://my-bucket/cache/arm.gcc@r2-darwin-arm64-blake3-abcdef0123456789.tar.zst");
}

TEST_CASE("package_depot_index: Windows line endings handled") {
  auto index{package_depot_index::build_from_contents({
      "https://cdn/pkg@v1-darwin-arm64-blake3-aaaa.tar.zst\r\n"
      "https://cdn/pkg@v2-linux-x86_64-blake3-bbbb.tar.zst\r\n"})};

  CHECK(index.find("pkg@v1", "darwin", "arm64", "aaaa").has_value());
  CHECK(index.find("pkg@v2", "linux", "x86_64", "bbbb").has_value());
}

TEST_CASE("package_depot_index: miss on wrong arch") {
  auto index{package_depot_index::build_from_contents(
      {"https://cdn.example.com/arm.gcc@r2-darwin-arm64-blake3-abcdef0123456789.tar.zst\n"})};

  CHECK_FALSE(index.find("arm.gcc@r2", "darwin", "x86_64", "abcdef0123456789").has_value());
}

TEST_CASE("package_depot_index: duplicate entries in same manifest keeps first") {
  auto index{package_depot_index::build_from_contents({
      "https://first/arm.gcc@r2-darwin-arm64-blake3-aaaa.tar.zst\n"
      "https://second/arm.gcc@r2-darwin-arm64-blake3-aaaa.tar.zst\n"})};

  auto result{index.find("arm.gcc@r2", "darwin", "arm64", "aaaa")};
  REQUIRE(result.has_value());
  CHECK(*result == "https://first/arm.gcc@r2-darwin-arm64-blake3-aaaa.tar.zst");
}

TEST_CASE("package_depot_index: whitespace-only lines skipped") {
  auto index{package_depot_index::build_from_contents({
      "   \n"
      "\t\n"
      "  \t  \n"
      "https://cdn/pkg@v1-darwin-arm64-blake3-aaaa.tar.zst\n"})};

  // Whitespace-only lines are not blank after trimming \r, but they don't start with '#'
  // and won't parse as valid archive URLs — they get warned and skipped
  CHECK(index.find("pkg@v1", "darwin", "arm64", "aaaa").has_value());
}

TEST_CASE("package_depot_index: bare filename without path separator") {
  auto index{package_depot_index::build_from_contents({
      "arm.gcc@r2-darwin-arm64-blake3-aaaa.tar.zst\n"})};

  auto result{index.find("arm.gcc@r2", "darwin", "arm64", "aaaa")};
  REQUIRE(result.has_value());
  CHECK(*result == "arm.gcc@r2-darwin-arm64-blake3-aaaa.tar.zst");
}

TEST_CASE("package_depot_index: manifest with only comments yields empty index") {
  auto index{package_depot_index::build_from_contents({
      "# This is just comments\n"
      "# Nothing real here\n"})};

  CHECK(index.empty());
}

TEST_CASE("package_depot_index: lines without .tar.zst extension skipped") {
  auto index{package_depot_index::build_from_contents({
      "https://cdn/arm.gcc@r2-darwin-arm64-blake3-aaaa.tar.gz\n"
      "https://cdn/pkg@v1-darwin-arm64-blake3-bbbb.tar.zst\n"})};

  CHECK_FALSE(index.find("arm.gcc@r2", "darwin", "arm64", "aaaa").has_value());
  CHECK(index.find("pkg@v1", "darwin", "arm64", "bbbb").has_value());
}

TEST_CASE("package_depot_index: find with empty identity returns nullopt") {
  auto index{package_depot_index::build_from_contents(
      {"https://cdn/pkg@v1-darwin-arm64-blake3-aaaa.tar.zst\n"})};

  CHECK_FALSE(index.find("", "darwin", "arm64", "aaaa").has_value());
}

TEST_CASE("package_depot_index: find with empty hash returns nullopt") {
  auto index{package_depot_index::build_from_contents(
      {"https://cdn/pkg@v1-darwin-arm64-blake3-aaaa.tar.zst\n"})};

  CHECK_FALSE(index.find("pkg@v1", "darwin", "arm64", "").has_value());
}

TEST_CASE("package_depot_index: URL with deep path structure") {
  auto index{package_depot_index::build_from_contents({
      "s3://bucket/a/b/c/d/e/arm.gcc@r2-darwin-arm64-blake3-aaaa.tar.zst\n"})};

  auto result{index.find("arm.gcc@r2", "darwin", "arm64", "aaaa")};
  REQUIRE(result.has_value());
  CHECK(*result ==
        "s3://bucket/a/b/c/d/e/arm.gcc@r2-darwin-arm64-blake3-aaaa.tar.zst");
}
