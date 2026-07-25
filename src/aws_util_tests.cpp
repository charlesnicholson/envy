#include "aws_util.h"

#include "doctest.h"

#include <stdexcept>

// --- aws_s3_parse_uri ---

TEST_CASE("aws_s3_parse_uri: bucket and key") {
  auto const parts{ envy::aws_s3_parse_uri("s3://my-bucket/releases/v1.2.3/envy.tar.gz",
                                           "test") };
  CHECK(parts.bucket == "my-bucket");
  CHECK(parts.key == "releases/v1.2.3/envy.tar.gz");
}

TEST_CASE("aws_s3_parse_uri: bucket only yields empty key") {
  // An upload target may name just a bucket; callers append their own prefix.
  auto const parts{ envy::aws_s3_parse_uri("s3://my-bucket", "test") };
  CHECK(parts.bucket == "my-bucket");
  CHECK(parts.key.empty());
}

TEST_CASE("aws_s3_parse_uri: bucket with trailing slash yields empty key") {
  auto const parts{ envy::aws_s3_parse_uri("s3://my-bucket/", "test") };
  CHECK(parts.bucket == "my-bucket");
  CHECK(parts.key.empty());
}

TEST_CASE("aws_s3_parse_uri: trailing slashes stripped from key") {
  // S3 keys are opaque bytes, so joining an unstripped "prefix/" with "/v1.2.3" would mint
  // "prefix//v1.2.3" -- a different object than the one readers ask for.
  CHECK(envy::aws_s3_parse_uri("s3://b/prefix/", "test").key == "prefix");
  CHECK(envy::aws_s3_parse_uri("s3://b/a/b/c///", "test").key == "a/b/c");
}

TEST_CASE("aws_s3_parse_uri: scheme match is case-insensitive") {
  // uri_classify accepts S3:// via istarts_with, so this must agree or a manifest mirror
  // classifies as S3 and then fails to parse.
  CHECK(envy::aws_s3_parse_uri("S3://b/k", "test").bucket == "b");
  CHECK(envy::aws_s3_parse_uri("s3://b/k", "test").bucket == "b");
  CHECK(envy::aws_s3_parse_uri("S3://B/K", "test").bucket == "B");
}

TEST_CASE("aws_s3_parse_uri: key case is preserved") {
  auto const parts{ envy::aws_s3_parse_uri("s3://Bucket/Mixed/Case/Key", "test") };
  CHECK(parts.bucket == "Bucket");
  CHECK(parts.key == "Mixed/Case/Key");
}

TEST_CASE("aws_s3_parse_uri: non-s3 scheme rejected") {
  CHECK_THROWS_AS(envy::aws_s3_parse_uri("https://example.com/x", "test"),
                  std::invalid_argument);
  CHECK_THROWS_AS(envy::aws_s3_parse_uri("/local/path", "test"), std::invalid_argument);
  CHECK_THROWS_AS(envy::aws_s3_parse_uri("", "test"), std::invalid_argument);
}

TEST_CASE("aws_s3_parse_uri: single-slash s3 typo rejected") {
  CHECK_THROWS_AS(envy::aws_s3_parse_uri("s3:/bucket/key", "test"), std::invalid_argument);
}

TEST_CASE("aws_s3_parse_uri: missing bucket rejected") {
  CHECK_THROWS_AS(envy::aws_s3_parse_uri("s3://", "test"), std::invalid_argument);
  CHECK_THROWS_AS(envy::aws_s3_parse_uri("s3:///key", "test"), std::invalid_argument);
}

TEST_CASE("aws_s3_parse_uri: op label appears in the error") {
  try {
    envy::aws_s3_parse_uri("nope", "mirror-envy");
    FAIL("expected throw");
  } catch (std::invalid_argument const &e) {
    CHECK(std::string{ e.what() }.starts_with("mirror-envy: "));
  }
}
