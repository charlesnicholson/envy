#include "phase_fetch.h"

#include "doctest.h"

#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>

namespace envy {

TEST_CASE("url_to_fetch_request HTTP") {
  auto req{ url_to_fetch_request("http://example.com/file.tar.gz",
                                 "/tmp/file.tar.gz",
                                 std::nullopt,
                                 "test") };

  REQUIRE(std::holds_alternative<fetch_request_http>(req));
  auto const &http_req{ std::get<fetch_request_http>(req) };
  CHECK(http_req.source == "http://example.com/file.tar.gz");
  CHECK(http_req.destination == "/tmp/file.tar.gz");
}

TEST_CASE("url_to_fetch_request HTTPS") {
  auto req{ url_to_fetch_request("https://example.com/file.tar.gz",
                                 "/tmp/file.tar.gz",
                                 std::nullopt,
                                 "test") };

  REQUIRE(std::holds_alternative<fetch_request_https>(req));
  auto const &https_req{ std::get<fetch_request_https>(req) };
  CHECK(https_req.source == "https://example.com/file.tar.gz");
  CHECK(https_req.destination == "/tmp/file.tar.gz");
}

TEST_CASE("url_to_fetch_request FTP") {
  auto req{ url_to_fetch_request("ftp://example.com/file.tar.gz",
                                 "/tmp/file.tar.gz",
                                 std::nullopt,
                                 "test") };

  REQUIRE(std::holds_alternative<fetch_request_ftp>(req));
  auto const &ftp_req{ std::get<fetch_request_ftp>(req) };
  CHECK(ftp_req.source == "ftp://example.com/file.tar.gz");
  CHECK(ftp_req.destination == "/tmp/file.tar.gz");
}

TEST_CASE("url_to_fetch_request FTPS") {
  auto req{ url_to_fetch_request("ftps://example.com/file.tar.gz",
                                 "/tmp/file.tar.gz",
                                 std::nullopt,
                                 "test") };

  REQUIRE(std::holds_alternative<fetch_request_ftps>(req));
  auto const &ftps_req{ std::get<fetch_request_ftps>(req) };
  CHECK(ftps_req.source == "ftps://example.com/file.tar.gz");
  CHECK(ftps_req.destination == "/tmp/file.tar.gz");
}

TEST_CASE("url_to_fetch_request S3") {
  auto req{ url_to_fetch_request("s3://bucket/file.tar.gz",
                                 "/tmp/file.tar.gz",
                                 std::nullopt,
                                 "test") };

  REQUIRE(std::holds_alternative<fetch_request_s3>(req));
  auto const &s3_req{ std::get<fetch_request_s3>(req) };
  CHECK(s3_req.source == "s3://bucket/file.tar.gz");
  CHECK(s3_req.destination == "/tmp/file.tar.gz");
}

TEST_CASE("url_to_fetch_request file absolute") {
  auto req{ url_to_fetch_request("/absolute/path/file.tar.gz",
                                 "/tmp/file.tar.gz",
                                 std::nullopt,
                                 "test") };

  REQUIRE(std::holds_alternative<fetch_request_file>(req));
  auto const &file_req{ std::get<fetch_request_file>(req) };
  CHECK(file_req.source == "/absolute/path/file.tar.gz");
  CHECK(file_req.destination == "/tmp/file.tar.gz");
}

TEST_CASE("url_to_fetch_request file relative") {
  auto req{ url_to_fetch_request("relative/path/file.tar.gz",
                                 "/tmp/file.tar.gz",
                                 std::nullopt,
                                 "test") };

  REQUIRE(std::holds_alternative<fetch_request_file>(req));
  auto const &file_req{ std::get<fetch_request_file>(req) };
  CHECK(file_req.source == "relative/path/file.tar.gz");
  CHECK(file_req.destination == "/tmp/file.tar.gz");
}

TEST_CASE("url_to_fetch_request git with ref") {
  auto req{ url_to_fetch_request("git://github.com/user/repo.git",
                                 "/tmp/repo",
                                 "abc123def456",
                                 "test") };

  REQUIRE(std::holds_alternative<fetch_request_git>(req));
  auto const &git_req{ std::get<fetch_request_git>(req) };
  CHECK(git_req.source == "git://github.com/user/repo.git");
  CHECK(git_req.destination == "/tmp/repo");
  CHECK(git_req.ref == "abc123def456");
}

TEST_CASE("url_to_fetch_request git without ref throws") {
  CHECK_THROWS_WITH_AS(url_to_fetch_request("git://github.com/user/repo.git",
                                            "/tmp/repo",
                                            std::nullopt,
                                            "test"),
                       "Git URLs require 'ref' field in test",
                       std::runtime_error);
}

TEST_CASE("url_to_fetch_request git with empty ref throws") {
  CHECK_THROWS_WITH_AS(
      url_to_fetch_request("git://github.com/user/repo.git", "/tmp/repo", "", "test"),
      "Git URLs require 'ref' field in test",
      std::runtime_error);
}

TEST_CASE("url_to_fetch_request unsupported scheme throws") {
  CHECK_THROWS_WITH_AS(
      url_to_fetch_request("unsupported://example.com/file",
                           "/tmp/file",
                           std::nullopt,
                           "test context"),
      "Unsupported URL scheme in test context: unsupported://example.com/file",
      std::runtime_error);
}

}  // namespace envy
