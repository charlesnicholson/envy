#include "cmd_mirror_envy.h"

#include "doctest.h"

#include <algorithm>
#include <stdexcept>
#include <string>

namespace {

constexpr std::string_view kFrom{ "https://example.com/releases" };

envy::mirror_envy_plan plan_for(std::string_view dest) {
  return envy::mirror_envy_make_plan("1.2.3", dest, kFrom);
}

bool has_relpath(envy::mirror_envy_plan const &plan, std::string_view relpath) {
  return std::ranges::any_of(plan.items,
                             [&](auto const &i) { return i.relpath == relpath; });
}

std::string url_for(envy::mirror_envy_plan const &plan, std::string_view relpath) {
  auto const it{ std::ranges::find_if(plan.items, [&](auto const &i) {
    return i.relpath == relpath;
  }) };
  return it == plan.items.end() ? std::string{} : it->source_url;
}

}  // namespace

// --- destination classification ---

TEST_CASE("mirror_envy_make_plan: relative local directory") {
  auto const plan{ plan_for("./stage") };
  CHECK_FALSE(plan.dest_is_s3);
  CHECK(plan.bucket.empty());
  CHECK_FALSE(plan.local_dir.empty());
}

TEST_CASE("mirror_envy_make_plan: absolute local directory") {
  auto const plan{ plan_for("/tmp/stage") };
  CHECK_FALSE(plan.dest_is_s3);
  CHECK_FALSE(plan.local_dir.empty());
}

TEST_CASE("mirror_envy_make_plan: s3 destination with prefix") {
  auto const plan{ plan_for("s3://my-bucket/releases") };
  CHECK(plan.dest_is_s3);
  CHECK(plan.bucket == "my-bucket");
  CHECK(plan.prefix == "releases");
}

TEST_CASE("mirror_envy_make_plan: s3 bucket root has an empty prefix") {
  auto const plan{ plan_for("s3://my-bucket") };
  CHECK(plan.dest_is_s3);
  CHECK(plan.bucket == "my-bucket");
  CHECK(plan.prefix.empty());
}

TEST_CASE("mirror_envy_make_plan: trailing slashes on an s3 prefix are normalized") {
  CHECK(plan_for("s3://my-bucket/releases/").prefix == "releases");
  CHECK(plan_for("s3://my-bucket/a/b///").prefix == "a/b");
}

TEST_CASE("mirror_envy_make_plan: uppercase S3 scheme accepted") {
  // uri_classify matches the scheme case-insensitively, so this must not fall through to
  // being treated as a local directory named "S3:".
  auto const plan{ plan_for("S3://my-bucket/releases") };
  CHECK(plan.dest_is_s3);
  CHECK(plan.bucket == "my-bucket");
}

TEST_CASE("mirror_envy_make_plan: single-slash s3 typo rejected, not staged locally") {
  // Without the guard this classifies as a relative path and silently stages into a local
  // directory literally named "s3:".
  CHECK_THROWS_AS(plan_for("s3:/my-bucket/releases"), std::runtime_error);
  CHECK_THROWS_AS(plan_for("S3:/my-bucket"), std::runtime_error);
  CHECK_THROWS_AS(plan_for("s3:"), std::runtime_error);
}

TEST_CASE("mirror_envy_make_plan: non-local non-s3 destinations rejected") {
  CHECK_THROWS_AS(plan_for("https://example.com/releases"), std::runtime_error);
  CHECK_THROWS_AS(plan_for("git://example.com/repo"), std::runtime_error);
  CHECK_THROWS_AS(plan_for("ssh://host/path"), std::runtime_error);
  // A dest ending in .git classifies as GIT; reject it loudly rather than confusingly.
  CHECK_THROWS_AS(plan_for("./mirror.git"), std::runtime_error);
  CHECK_THROWS_AS(plan_for(""), std::runtime_error);
}

// --- version and source validation ---

TEST_CASE("mirror_envy_make_plan: invalid versions rejected") {
  CHECK_THROWS_AS(envy::mirror_envy_make_plan("", "./stage", kFrom), std::runtime_error);
  CHECK_THROWS_AS(envy::mirror_envy_make_plan("../etc/passwd", "./stage", kFrom),
                  std::runtime_error);
  CHECK_THROWS_AS(envy::mirror_envy_make_plan("1.2.3 ; rm -rf /", "./stage", kFrom),
                  std::runtime_error);
}

TEST_CASE("mirror_envy_make_plan: empty source mirror rejected") {
  CHECK_THROWS_AS(envy::mirror_envy_make_plan("1.2.3", "./stage", ""), std::runtime_error);
  CHECK_THROWS_AS(envy::mirror_envy_make_plan("1.2.3", "./stage", "///"),
                  std::runtime_error);
}

// --- item set ---

TEST_CASE("mirror_envy_make_plan: covers every published release target") {
  auto const plan{ plan_for("./stage") };
  CHECK(plan.items.size() == 6);
  CHECK(has_relpath(plan, "v1.2.3/envy-darwin-arm64.tar.gz"));
  CHECK(has_relpath(plan, "v1.2.3/envy-darwin-x86_64.tar.gz"));
  CHECK(has_relpath(plan, "v1.2.3/envy-linux-arm64.tar.gz"));
  CHECK(has_relpath(plan, "v1.2.3/envy-linux-x86_64.tar.gz"));
  CHECK(has_relpath(plan, "v1.2.3/envy-windows-arm64.zip"));
  CHECK(has_relpath(plan, "v1.2.3/envy-windows-x86_64.zip"));
}

TEST_CASE("mirror_envy_make_plan: windows archives are named from any host") {
  auto const plan{ plan_for("./stage") };
  CHECK(url_for(plan, "v1.2.3/envy-windows-x86_64.zip") ==
        "https://example.com/releases/v1.2.3/envy-windows-x86_64.zip");
}

TEST_CASE("mirror_envy_make_plan: source urls hang off the from mirror") {
  auto const plan{ plan_for("./stage") };
  CHECK(url_for(plan, "v1.2.3/envy-linux-arm64.tar.gz") ==
        "https://example.com/releases/v1.2.3/envy-linux-arm64.tar.gz");
}

TEST_CASE("mirror_envy_make_plan: trailing slash on the source mirror is stripped") {
  // Otherwise every source url carries a double slash, which is a distinct (missing) key
  // for an s3:// source.
  auto const plan{
    envy::mirror_envy_make_plan("1.2.3", "./stage", "s3://src-bucket/releases/")
  };
  CHECK(url_for(plan, "v1.2.3/envy-linux-arm64.tar.gz") ==
        "s3://src-bucket/releases/v1.2.3/envy-linux-arm64.tar.gz");
}

// --- s3 key construction ---

TEST_CASE("mirror_envy_s3_root: prefix and bucket-root forms") {
  CHECK(mirror_envy_s3_root(plan_for("s3://b/releases")) == "s3://b/releases");
  CHECK(mirror_envy_s3_root(plan_for("s3://b/releases/")) == "s3://b/releases");
  CHECK(mirror_envy_s3_root(plan_for("s3://b")) == "s3://b");
}

TEST_CASE("mirror_envy_s3_uri: joins without minting a double-slash key") {
  auto const with_prefix{ plan_for("s3://b/releases/") };
  CHECK(mirror_envy_s3_uri(with_prefix, "v1.2.3/envy-linux-arm64.tar.gz") ==
        "s3://b/releases/v1.2.3/envy-linux-arm64.tar.gz");
  CHECK(mirror_envy_s3_uri(with_prefix, "latest") == "s3://b/releases/latest");

  auto const bucket_root{ plan_for("s3://b") };
  CHECK(mirror_envy_s3_uri(bucket_root, "v1.2.3/envy-linux-arm64.tar.gz") ==
        "s3://b/v1.2.3/envy-linux-arm64.tar.gz");
  CHECK(mirror_envy_s3_uri(bucket_root, "latest") == "s3://b/latest");
}

TEST_CASE("mirror_envy_s3_uri: every planned key is reachable at the mirror root") {
  // The uploaded keys must be exactly what envy_release_url will later ask for.
  auto const plan{ plan_for("s3://b/releases") };
  auto const root{ mirror_envy_s3_root(plan) };
  for (auto const &item : plan.items) {
    CHECK(mirror_envy_s3_uri(plan, item.relpath) == root + "/" + item.relpath);
    CHECK(mirror_envy_s3_uri(plan, item.relpath).find("//v") == std::string::npos);
  }
}
