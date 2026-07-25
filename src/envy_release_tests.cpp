#include "envy_release.h"

#include "doctest.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <string_view>

// --- upstream location constants ---

TEST_CASE("release URLs derive from one upstream repo") {
  // Guards the one-line-relocation property: both constants share a base, so moving the
  // project to a new org means editing only ENVY_UPSTREAM_REPO_URL.
  constexpr std::string_view kDownloadSuffix{ "/releases/download" };
  constexpr std::string_view kLatestSuffix{ "/releases/latest" };
  REQUIRE(envy::kEnvyReleaseDownloadUrl.ends_with(kDownloadSuffix));
  REQUIRE(envy::kEnvyReleaseLatestUrl.ends_with(kLatestSuffix));

  auto const base{ envy::kEnvyReleaseDownloadUrl.substr(
      0,
      envy::kEnvyReleaseDownloadUrl.size() - kDownloadSuffix.size()) };
  CHECK(envy::kEnvyReleaseLatestUrl.starts_with(base));
  CHECK_FALSE(base.empty());
}

// --- envy_release_validate_mirror ---

TEST_CASE("envy_release_validate_mirror: ordinary mirrors accepted") {
  envy::envy_release_validate_mirror("https://github.com/org/envy/releases/download", "t");
  envy::envy_release_validate_mirror("s3://my-envy-mirror/releases", "t");
  envy::envy_release_validate_mirror("file:///tmp/releases", "t");
  // Characters that are fine in every consumer: query strings, ports, tildes, percent.
  envy::envy_release_validate_mirror("https://h:8443/a-b_c.d~e%20f?x=1&y=2", "t");
}

TEST_CASE("envy_release_validate_mirror: newline injection rejected") {
  // Would otherwise append arbitrary directives to envy.lua via the manifest stamp.
  CHECK_THROWS_AS(
      envy::envy_release_validate_mirror("https://x\"\n-- @envy version \"9.9.9", "t"),
      std::runtime_error);
  CHECK_THROWS_AS(envy::envy_release_validate_mirror("https://x\ny", "t"),
                  std::runtime_error);
  CHECK_THROWS_AS(envy::envy_release_validate_mirror("https://x\ry", "t"),
                  std::runtime_error);
}

TEST_CASE("envy_release_validate_mirror: quote and backslash rejected") {
  // The directive and the shell/batch assignments are all double-quoted.
  CHECK_THROWS_AS(envy::envy_release_validate_mirror("https://x\"y", "t"),
                  std::runtime_error);
  CHECK_THROWS_AS(envy::envy_release_validate_mirror("https://x\\y", "t"),
                  std::runtime_error);
}

TEST_CASE("envy_release_validate_mirror: batch delayed-expansion bang rejected") {
  // envy.bat runs under EnableDelayedExpansion, where `!` is a variable delimiter.
  CHECK_THROWS_AS(envy::envy_release_validate_mirror("https://x/a!b", "t"),
                  std::runtime_error);
}

TEST_CASE("envy_release_validate_mirror: control characters and empty rejected") {
  CHECK_THROWS_AS(envy::envy_release_validate_mirror("https://x\ty", "t"),
                  std::runtime_error);
  CHECK_THROWS_AS(envy::envy_release_validate_mirror(std::string_view{ "a\0b", 3 }, "t"),
                  std::runtime_error);
  CHECK_THROWS_AS(envy::envy_release_validate_mirror("", "t"), std::runtime_error);
}

TEST_CASE("envy_release_validate_mirror: op label appears in the error") {
  try {
    envy::envy_release_validate_mirror("bad\nvalue", "init");
    FAIL("expected throw");
  } catch (std::runtime_error const &e) {
    CHECK(std::string{ e.what() }.starts_with("init: "));
  }
}

// --- envy_release_version_is_valid ---

TEST_CASE("envy_release_version_is_valid: normal version") {
  CHECK(envy::envy_release_version_is_valid("1.2.3"));
}

TEST_CASE("envy_release_version_is_valid: version with pre-release suffix") {
  CHECK(envy::envy_release_version_is_valid("1.2.3-beta.1"));
}

TEST_CASE("envy_release_version_is_valid: version with underscore") {
  CHECK(envy::envy_release_version_is_valid("1_2_3"));
}

TEST_CASE("envy_release_version_is_valid: empty string rejected") {
  CHECK_FALSE(envy::envy_release_version_is_valid(""));
}

TEST_CASE("envy_release_version_is_valid: path traversal rejected") {
  CHECK_FALSE(envy::envy_release_version_is_valid("../../../etc/passwd"));
}

TEST_CASE("envy_release_version_is_valid: slash rejected") {
  CHECK_FALSE(envy::envy_release_version_is_valid("1.2.3/evil"));
}

TEST_CASE("envy_release_version_is_valid: backslash rejected") {
  CHECK_FALSE(envy::envy_release_version_is_valid("1.2.3\\evil"));
}

TEST_CASE("envy_release_version_is_valid: space rejected") {
  CHECK_FALSE(envy::envy_release_version_is_valid("1.2.3 ; rm -rf /"));
}

TEST_CASE("envy_release_version_is_valid: non-ASCII rejected regardless of locale") {
  // Version becomes the envy/<version> cache path component; high-bit UTF-8 bytes
  // must be rejected (std::isalnum could accept them under a non-"C" locale).
  CHECK_FALSE(envy::envy_release_version_is_valid("1.2.3-caf\xc3\xa9"));  // "café"
  CHECK_FALSE(envy::envy_release_version_is_valid("\xe4\xbd\xa0"));       // "你"
}

TEST_CASE("envy_release_version_is_valid: null byte rejected") {
  CHECK_FALSE(envy::envy_release_version_is_valid(std::string_view{ "1.2\0.3", 6 }));
}

// --- envy_release_archive_ext / envy_release_archive_name ---

// The extension is keyed on the target os, never the host: mirroring the full release set
// from any one machine has to name the windows artifacts correctly.

TEST_CASE("envy_release_archive_ext: windows is zip, posix is tar.gz") {
  CHECK(envy::envy_release_archive_ext("windows") == ".zip");
  CHECK(envy::envy_release_archive_ext("darwin") == ".tar.gz");
  CHECK(envy::envy_release_archive_ext("linux") == ".tar.gz");
}

TEST_CASE("envy_release_archive_name: every published release target") {
  CHECK(envy::envy_release_archive_name("darwin", "arm64") == "envy-darwin-arm64.tar.gz");
  CHECK(envy::envy_release_archive_name("darwin", "x86_64") ==
        "envy-darwin-x86_64.tar.gz");
  CHECK(envy::envy_release_archive_name("linux", "arm64") == "envy-linux-arm64.tar.gz");
  CHECK(envy::envy_release_archive_name("linux", "x86_64") == "envy-linux-x86_64.tar.gz");
  CHECK(envy::envy_release_archive_name("windows", "arm64") == "envy-windows-arm64.zip");
  CHECK(envy::envy_release_archive_name("windows", "x86_64") == "envy-windows-x86_64.zip");
}

TEST_CASE("kEnvyReleaseTargets: matches the release workflow's asset matrix") {
  // Names must stay byte-identical to .github/workflows/release.yml or mirrors 404.
  CHECK(envy::kEnvyReleaseTargets.size() == 6);

  auto const has{ [](std::string_view os, std::string_view arch) {
    return std::ranges::any_of(envy::kEnvyReleaseTargets, [&](auto const &t) {
      return t.os == os && t.arch == arch;
    });
  } };
  CHECK(has("darwin", "arm64"));
  CHECK(has("darwin", "x86_64"));
  CHECK(has("linux", "arm64"));
  CHECK(has("linux", "x86_64"));
  CHECK(has("windows", "arm64"));
  CHECK(has("windows", "x86_64"));
}

// --- envy_release_url ---

// Derived from the constant, not a second copy of the URL: the point of centralizing it is
// that relocating the project does not require editing tests too.
TEST_CASE("envy_release_url: default mirror darwin arm64") {
  auto const url{
    envy::envy_release_url(envy::kEnvyReleaseDownloadUrl, "1.2.3", "darwin", "arm64")
  };
  CHECK(url ==
        std::string{ envy::kEnvyReleaseDownloadUrl } + "/v1.2.3/envy-darwin-arm64.tar.gz");
}

TEST_CASE("envy_release_url: linux x86_64") {
  auto const url{
    envy::envy_release_url(envy::kEnvyReleaseDownloadUrl, "2.0.0", "linux", "x86_64")
  };
  CHECK(url ==
        std::string{ envy::kEnvyReleaseDownloadUrl } + "/v2.0.0/envy-linux-x86_64.tar.gz");
}

TEST_CASE("envy_release_url: windows names a zip from any host") {
  auto const url{ envy::envy_release_url("https://my-mirror.example.com/envy",
                                         "2.0.0",
                                         "windows",
                                         "x86_64") };
  CHECK(url == "https://my-mirror.example.com/envy/v2.0.0/envy-windows-x86_64.zip");
}

TEST_CASE("envy_release_url: custom mirror") {
  auto const url{ envy::envy_release_url("https://my-mirror.example.com/envy",
                                         "2.0.0",
                                         "linux",
                                         "x86_64") };
  CHECK(url == "https://my-mirror.example.com/envy/v2.0.0/envy-linux-x86_64.tar.gz");
}

TEST_CASE("envy_release_url: file mirror") {
  auto const url{
    envy::envy_release_url("file:///tmp/releases", "1.0.0", "darwin", "arm64")
  };
  CHECK(url == "file:///tmp/releases/v1.0.0/envy-darwin-arm64.tar.gz");
}

TEST_CASE("envy_release_url: s3 mirror") {
  auto const url{
    envy::envy_release_url("s3://my-bucket/envy-releases", "3.1.0", "linux", "arm64")
  };
  CHECK(url == "s3://my-bucket/envy-releases/v3.1.0/envy-linux-arm64.tar.gz");
}

TEST_CASE("envy_release_url: trailing slash on mirror produces double slash") {
  // Callers must strip trailing slashes: for s3:// this would be a distinct (missing) key.
  auto const url{
    envy::envy_release_url("https://mirror.example.com/", "1.0.0", "darwin", "arm64")
  };
  CHECK(url == "https://mirror.example.com//v1.0.0/envy-darwin-arm64.tar.gz");
}
