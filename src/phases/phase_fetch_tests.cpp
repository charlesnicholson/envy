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
                                 std::nullopt,
                                 "test") };

  REQUIRE(std::holds_alternative<fetch_request_http>(req));
  auto const &http_req{ std::get<fetch_request_http>(req) };
  CHECK(http_req.source == "http://example.com/file.tar.gz");
  CHECK(http_req.destination == "/tmp/file.tar.gz");
  CHECK_FALSE(http_req.post_data.has_value());
}

TEST_CASE("url_to_fetch_request HTTPS") {
  auto req{ url_to_fetch_request("https://example.com/file.tar.gz",
                                 "/tmp/file.tar.gz",
                                 std::nullopt,
                                 std::nullopt,
                                 "test") };

  REQUIRE(std::holds_alternative<fetch_request_https>(req));
  auto const &https_req{ std::get<fetch_request_https>(req) };
  CHECK(https_req.source == "https://example.com/file.tar.gz");
  CHECK(https_req.destination == "/tmp/file.tar.gz");
  CHECK_FALSE(https_req.post_data.has_value());
}

TEST_CASE("url_to_fetch_request FTP") {
  auto req{ url_to_fetch_request("ftp://example.com/file.tar.gz",
                                 "/tmp/file.tar.gz",
                                 std::nullopt,
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
                                 std::nullopt,
                                 "test") };

  REQUIRE(std::holds_alternative<fetch_request_git>(req));
  auto const &git_req{ std::get<fetch_request_git>(req) };
  CHECK(git_req.source == "git://github.com/user/repo.git");
  CHECK(git_req.destination == "/tmp/repo");
  CHECK(git_req.ref == "abc123def456");
  CHECK(git_req.scheme == uri_scheme::GIT);
}

TEST_CASE("url_to_fetch_request git_https with ref") {
  auto req{ url_to_fetch_request("https://github.com/user/repo.git",
                                 "/tmp/repo",
                                 "main",
                                 std::nullopt,
                                 "test") };

  REQUIRE(std::holds_alternative<fetch_request_git>(req));
  auto const &git_req{ std::get<fetch_request_git>(req) };
  CHECK(git_req.source == "https://github.com/user/repo.git");
  CHECK(git_req.destination == "/tmp/repo");
  CHECK(git_req.ref == "main");
  CHECK(git_req.scheme == uri_scheme::GIT_HTTPS);
}

TEST_CASE("url_to_fetch_request git without ref throws") {
  CHECK_THROWS_WITH_AS(url_to_fetch_request("git://github.com/user/repo.git",
                                            "/tmp/repo",
                                            std::nullopt,
                                            std::nullopt,
                                            "test"),
                       "Git URLs require 'ref' field in test",
                       std::runtime_error);
}

TEST_CASE("url_to_fetch_request git with empty ref throws") {
  CHECK_THROWS_WITH_AS(url_to_fetch_request("git://github.com/user/repo.git",
                                            "/tmp/repo",
                                            "",
                                            std::nullopt,
                                            "test"),
                       "Git URLs require 'ref' field in test",
                       std::runtime_error);
}

TEST_CASE("url_to_fetch_request unsupported scheme throws") {
  CHECK_THROWS_WITH_AS(
      url_to_fetch_request("unsupported://example.com/file",
                           "/tmp/file",
                           std::nullopt,
                           std::nullopt,
                           "test context"),
      "Unsupported URL scheme in test context: unsupported://example.com/file",
      std::runtime_error);
}

TEST_CASE("url_to_fetch_request HTTP with post_data") {
  auto req{ url_to_fetch_request("http://example.com/download",
                                 "/tmp/file",
                                 std::nullopt,
                                 "key=value",
                                 "test") };

  REQUIRE(std::holds_alternative<fetch_request_http>(req));
  auto const &http_req{ std::get<fetch_request_http>(req) };
  REQUIRE(http_req.post_data.has_value());
  CHECK(http_req.post_data.value() == "key=value");
}

TEST_CASE("url_to_fetch_request HTTPS with post_data") {
  auto req{ url_to_fetch_request("https://example.com/download",
                                 "/tmp/file",
                                 std::nullopt,
                                 "accept_license=yes",
                                 "test") };

  REQUIRE(std::holds_alternative<fetch_request_https>(req));
  auto const &https_req{ std::get<fetch_request_https>(req) };
  REQUIRE(https_req.post_data.has_value());
  CHECK(https_req.post_data.value() == "accept_license=yes");
}

TEST_CASE("url_to_fetch_request post_data on FTP throws") {
  CHECK_THROWS_WITH_AS(url_to_fetch_request("ftp://example.com/file",
                                            "/tmp/file",
                                            std::nullopt,
                                            "data=value",
                                            "test"),
                       "post_data only valid for HTTP/HTTPS in test",
                       std::runtime_error);
}

TEST_CASE("url_to_fetch_request post_data on git throws") {
  CHECK_THROWS_WITH_AS(url_to_fetch_request("git://github.com/user/repo.git",
                                            "/tmp/repo",
                                            "main",
                                            "data=value",
                                            "test"),
                       "post_data only valid for HTTP/HTTPS in test",
                       std::runtime_error);
}

TEST_CASE("url_to_fetch_request post_data on file throws") {
  CHECK_THROWS_WITH_AS(url_to_fetch_request("/path/to/file",
                                            "/tmp/file",
                                            std::nullopt,
                                            "data=value",
                                            "test"),
                       "post_data only valid for HTTP/HTTPS in test",
                       std::runtime_error);
}

}  // namespace envy
