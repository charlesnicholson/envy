#include "cmd_import.h"

#include "doctest.h"

#include <stdexcept>
#include <string_view>

using namespace envy;

TEST_CASE("parse_export_filename: simple identity") {
  auto const r{ parse_export_filename("arm.gcc@r2-darwin-arm64-blake3-abcdef0123456789") };
  CHECK(r.identity == "arm.gcc@r2");
  CHECK(r.platform == "darwin");
  CHECK(r.arch == "arm64");
  CHECK(r.hash_prefix == "abcdef0123456789");
}

TEST_CASE("parse_export_filename: hyphenated name") {
  auto const r{
    parse_export_filename("ns.my-tool@r10-linux-x86_64-blake3-0123456789abcdef")
  };
  CHECK(r.identity == "ns.my-tool@r10");
  CHECK(r.platform == "linux");
  CHECK(r.arch == "x86_64");
  CHECK(r.hash_prefix == "0123456789abcdef");
}

TEST_CASE("parse_export_filename: windows platform") {
  auto const r{
    parse_export_filename("core.python@r1-windows-x86_64-blake3-deadbeef")
  };
  CHECK(r.identity == "core.python@r1");
  CHECK(r.platform == "windows");
  CHECK(r.arch == "x86_64");
  CHECK(r.hash_prefix == "deadbeef");
}

TEST_CASE("parse_export_filename: missing @ throws") {
  CHECK_THROWS_AS(parse_export_filename("arm.gcc-r2-darwin-arm64-blake3-abcdef"),
                  std::runtime_error);
}

TEST_CASE("parse_export_filename: missing variant throws") {
  CHECK_THROWS_AS(parse_export_filename("arm.gcc@r2"), std::runtime_error);
}

TEST_CASE("parse_export_filename: missing blake3 tag throws") {
  CHECK_THROWS_AS(
      parse_export_filename("arm.gcc@r2-darwin-arm64-sha256-abcdef"),
      std::runtime_error);
}

TEST_CASE("parse_export_filename: empty hash prefix throws") {
  CHECK_THROWS_AS(parse_export_filename("arm.gcc@r2-darwin-arm64-blake3-"),
                  std::runtime_error);
}

TEST_CASE("parse_export_filename: empty platform throws") {
  CHECK_THROWS_AS(parse_export_filename("arm.gcc@r2--arm64-blake3-abcdef"),
                  std::runtime_error);
}

TEST_CASE("parse_export_filename: empty arch throws") {
  CHECK_THROWS_AS(parse_export_filename("arm.gcc@r2-darwin--blake3-abcdef"),
                  std::runtime_error);
}
