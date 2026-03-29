#include "cmd_merge_depot.h"

#include "doctest.h"

#include <sstream>
#include <stdexcept>

namespace { char const *const kFixtureDir = "test_data/merge_depot"; }  // namespace

TEST_CASE("parse_depot_manifest: valid entries") {
  auto entries{ envy::parse_depot_manifest(std::string(kFixtureDir) +
                                           "/valid_two_entries.txt") };

  REQUIRE(entries.size() == 2);
  CHECK(entries[0].hash ==
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
  CHECK(entries[0].path == "pkg-darwin-arm64.tar.zst");
  CHECK(entries[1].hash ==
        "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
  CHECK(entries[1].path == "pkg-linux-x86_64.tar.zst");
}

TEST_CASE("parse_depot_manifest: lowercases mixed-case hashes") {
  auto entries{ envy::parse_depot_manifest(std::string(kFixtureDir) +
                                           "/mixed_case_hash.txt") };

  REQUIRE(entries.size() == 1);
  CHECK(entries[0].hash ==
        "aabbccdd00112233445566778899aabbccdd00112233445566778899aabbccdd");
}

TEST_CASE("parse_depot_manifest: skips empty lines and comments") {
  auto entries{ envy::parse_depot_manifest(std::string(kFixtureDir) +
                                           "/comments_and_blanks.txt") };

  REQUIRE(entries.size() == 1);
  CHECK(entries[0].path == "pkg.tar.zst");
}

TEST_CASE("parse_depot_manifest: skips malformed lines") {
  auto entries{ envy::parse_depot_manifest(std::string(kFixtureDir) +
                                           "/malformed_lines.txt") };

  REQUIRE(entries.size() == 1);
  CHECK(entries[0].path == "good.tar.zst");
}

TEST_CASE("parse_depot_manifest: handles CRLF line endings") {
  auto entries{ envy::parse_depot_manifest(std::string(kFixtureDir) +
                                           "/crlf_endings.txt") };

  REQUIRE(entries.size() == 1);
  CHECK(entries[0].path == "pkg.tar.zst");
}

TEST_CASE("parse_depot_manifest: throws on nonexistent file") {
  CHECK_THROWS_AS(envy::parse_depot_manifest("/nonexistent/file.txt"), std::runtime_error);
}

TEST_CASE("parse_depot_manifest: empty file returns empty vector") {
  auto entries{ envy::parse_depot_manifest(std::string(kFixtureDir) + "/empty.txt") };

  CHECK(entries.empty());
}

TEST_CASE("parse_depot_manifest: preserves url paths with depot prefix") {
  auto entries{ envy::parse_depot_manifest(std::string(kFixtureDir) + "/url_paths.txt") };

  REQUIRE(entries.size() == 1);
  CHECK(entries[0].path == "https://cdn.example.com/depot/pkg@v1-darwin-arm64.tar.zst");
}

TEST_CASE("parse_s3_ls_lines: extracts keys from standard output") {
  auto input{ std::istringstream{
      "2024-01-15 12:34:56       1234 pkg-darwin-arm64.tar.zst\n"
      "2024-01-15 12:34:57       5678 pkg-linux-x86_64.tar.zst\n"} };

  auto keys{ envy::parse_s3_ls_lines(input) };

  CHECK(keys.size() == 2);
  CHECK(keys.count("pkg-darwin-arm64.tar.zst") == 1);
  CHECK(keys.count("pkg-linux-x86_64.tar.zst") == 1);
}

TEST_CASE("parse_s3_ls_lines: skips PRE lines") {
  auto input{ std::istringstream{
      "                           PRE some-prefix/\n"
      "2024-01-15 12:34:56       1234 actual-key.tar.zst\n"} };

  auto keys{ envy::parse_s3_ls_lines(input) };

  CHECK(keys.size() == 1);
  CHECK(keys.count("actual-key.tar.zst") == 1);
}

TEST_CASE("parse_s3_ls_lines: skips empty lines") {
  auto input{ std::istringstream{
      "\n"
      "2024-01-15 12:34:56       1234 pkg.tar.zst\n"
      "\n"} };

  auto keys{ envy::parse_s3_ls_lines(input) };

  CHECK(keys.size() == 1);
  CHECK(keys.count("pkg.tar.zst") == 1);
}

TEST_CASE("parse_s3_ls_lines: handles CRLF") {
  auto input{ std::istringstream{
      "2024-01-15 12:34:56       1234 pkg.tar.zst\r\n"} };

  auto keys{ envy::parse_s3_ls_lines(input) };

  CHECK(keys.size() == 1);
  CHECK(keys.count("pkg.tar.zst") == 1);
}

TEST_CASE("parse_s3_ls_lines: deduplicates entries") {
  auto input{ std::istringstream{
      "2024-01-15 12:34:56       1234 dup.tar.zst\n"
      "2024-01-15 12:34:57       1234 dup.tar.zst\n"} };

  auto keys{ envy::parse_s3_ls_lines(input) };

  CHECK(keys.size() == 1);
}

TEST_CASE("parse_s3_ls_lines: handles zero-byte objects") {
  auto input{ std::istringstream{
      "2024-01-15 12:34:56          0 empty-object\n"} };

  auto keys{ envy::parse_s3_ls_lines(input) };

  CHECK(keys.size() == 1);
  CHECK(keys.count("empty-object") == 1);
}

TEST_CASE("parse_s3_ls_lines: empty input returns empty set") {
  auto input{ std::istringstream{ "" } };

  auto keys{ envy::parse_s3_ls_lines(input) };

  CHECK(keys.empty());
}

TEST_CASE("parse_s3_ls_lines: handles varying size widths") {
  auto input{ std::istringstream{
      "2026-03-29 01:02:49  182643143 toolchain-darwin-arm64.tar.zst\n"
      "2026-03-29 11:17:13   67997829 sdk-linux-x86_64.tar.zst\n"
      "2026-03-29 11:17:20       7561 packages.txt\n"
      "2026-03-08 16:14:43   16961663 valgrind-linux-arm64.tar.zst\n"} };

  auto keys{ envy::parse_s3_ls_lines(input) };

  CHECK(keys.size() == 4);
  CHECK(keys.count("toolchain-darwin-arm64.tar.zst") == 1);
  CHECK(keys.count("sdk-linux-x86_64.tar.zst") == 1);
  CHECK(keys.count("packages.txt") == 1);
  CHECK(keys.count("valgrind-linux-arm64.tar.zst") == 1);
}

TEST_CASE("parse_s3_ls_lines: skips malformed lines") {
  auto input{ std::istringstream{
      "too short\n"
      "2024-01-15 12:34:56       1234 good.tar.zst\n"} };

  auto keys{ envy::parse_s3_ls_lines(input) };

  CHECK(keys.size() == 1);
  CHECK(keys.count("good.tar.zst") == 1);
}
