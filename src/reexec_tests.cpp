#include "reexec.h"

#include "doctest.h"

#ifdef _WIN32
#define EXT ".zip"
#else
#define EXT ".tar.gz"
#endif

// --- reexec_should decision logic ---

TEST_CASE("reexec_should: no @envy version returns PROCEED") {
  CHECK(envy::reexec_should("2.0.0", std::nullopt, false, false) ==
        envy::reexec_decision::PROCEED);
}

TEST_CASE("reexec_should: dev build 0.0.0 returns PROCEED") {
  CHECK(envy::reexec_should("0.0.0", std::string{ "1.5.0" }, false, false) ==
        envy::reexec_decision::PROCEED);
}

TEST_CASE("reexec_should: version match returns PROCEED") {
  CHECK(envy::reexec_should("1.5.0", std::string{ "1.5.0" }, false, false) ==
        envy::reexec_decision::PROCEED);
}

TEST_CASE("reexec_should: ENVY_REEXEC set returns PROCEED") {
  CHECK(envy::reexec_should("2.0.0", std::string{ "1.5.0" }, true, false) ==
        envy::reexec_decision::PROCEED);
}

TEST_CASE("reexec_should: ENVY_NO_REEXEC set returns PROCEED") {
  CHECK(envy::reexec_should("2.0.0", std::string{ "1.5.0" }, false, true) ==
        envy::reexec_decision::PROCEED);
}

TEST_CASE("reexec_should: both ENVY_REEXEC and ENVY_NO_REEXEC set returns PROCEED") {
  CHECK(envy::reexec_should("2.0.0", std::string{ "1.5.0" }, true, true) ==
        envy::reexec_decision::PROCEED);
}

TEST_CASE("reexec_should: version mismatch (downgrade) returns REEXEC") {
  CHECK(envy::reexec_should("2.0.0", std::string{ "1.5.0" }, false, false) ==
        envy::reexec_decision::REEXEC);
}

TEST_CASE("reexec_should: version mismatch (upgrade) returns REEXEC") {
  CHECK(envy::reexec_should("1.0.0", std::string{ "2.0.0" }, false, false) ==
        envy::reexec_decision::REEXEC);
}

TEST_CASE("reexec_should: empty requested version string triggers REEXEC") {
  // optional with empty string is still a value; "" != "2.0.0" → mismatch
  CHECK(envy::reexec_should("2.0.0", std::string{ "" }, false, false) ==
        envy::reexec_decision::REEXEC);
}

TEST_CASE("reexec_should: dev build 0.0.0 even with REEXEC flag returns PROCEED") {
  // Dev build check comes before REEXEC flag check — 0.0.0 always wins
  CHECK(envy::reexec_should("0.0.0", std::string{ "1.5.0" }, true, false) ==
        envy::reexec_decision::PROCEED);
}

TEST_CASE("reexec_should: ENVY_NO_REEXEC takes priority over version mismatch") {
  // no_reexec is checked before version comparison
  CHECK(envy::reexec_should("2.0.0", std::string{ "1.5.0" }, false, true) ==
        envy::reexec_decision::PROCEED);
}

// --- reexec_is_valid_version ---

TEST_CASE("reexec_is_valid_version: normal version") {
  CHECK(envy::reexec_is_valid_version("1.2.3"));
}

TEST_CASE("reexec_is_valid_version: version with pre-release suffix") {
  CHECK(envy::reexec_is_valid_version("1.2.3-beta.1"));
}

TEST_CASE("reexec_is_valid_version: version with underscore") {
  CHECK(envy::reexec_is_valid_version("1_2_3"));
}

TEST_CASE("reexec_is_valid_version: empty string rejected") {
  CHECK_FALSE(envy::reexec_is_valid_version(""));
}

TEST_CASE("reexec_is_valid_version: path traversal rejected") {
  CHECK_FALSE(envy::reexec_is_valid_version("../../../etc/passwd"));
}

TEST_CASE("reexec_is_valid_version: slash rejected") {
  CHECK_FALSE(envy::reexec_is_valid_version("1.2.3/evil"));
}

TEST_CASE("reexec_is_valid_version: backslash rejected") {
  CHECK_FALSE(envy::reexec_is_valid_version("1.2.3\\evil"));
}

TEST_CASE("reexec_is_valid_version: space rejected") {
  CHECK_FALSE(envy::reexec_is_valid_version("1.2.3 ; rm -rf /"));
}

TEST_CASE("reexec_is_valid_version: null byte rejected") {
  CHECK_FALSE(envy::reexec_is_valid_version(std::string_view{ "1.2\0.3", 6 }));
}

// --- reexec_download_url ---

TEST_CASE("reexec_download_url: default mirror darwin arm64") {
  auto const url{ envy::reexec_download_url(
      "https://github.com/charlesnicholson/envy/releases/download",
      "1.2.3",
      "darwin",
      "arm64") };
  CHECK(url ==
        "https://github.com/charlesnicholson/envy/releases/download"
        "/v1.2.3/envy-darwin-arm64" EXT);
}

TEST_CASE("reexec_download_url: linux x86_64") {
  auto const url{ envy::reexec_download_url(
      "https://github.com/charlesnicholson/envy/releases/download",
      "2.0.0",
      "linux",
      "x86_64") };
  CHECK(url ==
        "https://github.com/charlesnicholson/envy/releases/download"
        "/v2.0.0/envy-linux-x86_64" EXT);
}

TEST_CASE("reexec_download_url: custom mirror") {
  auto const url{ envy::reexec_download_url("https://my-mirror.example.com/envy",
                                            "2.0.0",
                                            "linux",
                                            "x86_64") };
  CHECK(url == "https://my-mirror.example.com/envy/v2.0.0/envy-linux-x86_64" EXT);
}

TEST_CASE("reexec_download_url: file mirror") {
  auto const url{
    envy::reexec_download_url("file:///tmp/releases", "1.0.0", "darwin", "arm64")
  };
  CHECK(url == "file:///tmp/releases/v1.0.0/envy-darwin-arm64" EXT);
}

TEST_CASE("reexec_download_url: s3 mirror") {
  auto const url{
    envy::reexec_download_url("s3://my-bucket/envy-releases", "3.1.0", "linux", "arm64")
  };
  CHECK(url == "s3://my-bucket/envy-releases/v3.1.0/envy-linux-arm64" EXT);
}

TEST_CASE("reexec_download_url: trailing slash on mirror produces double slash") {
  // Callers should not pass trailing slashes, but document the behavior
  auto const url{
    envy::reexec_download_url("https://mirror.example.com/", "1.0.0", "darwin", "arm64")
  };
  CHECK(url == "https://mirror.example.com//v1.0.0/envy-darwin-arm64" EXT);
}
