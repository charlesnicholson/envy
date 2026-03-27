#include "cmd_merge_depot.h"

#include "doctest.h"

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
