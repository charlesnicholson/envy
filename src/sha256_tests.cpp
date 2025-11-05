#include "sha256.h"

#include "doctest.h"

#include <array>
#include <filesystem>

namespace fs = std::filesystem;

namespace {

constexpr envy::sha256_t kExpectedSha256Abc{
  0xBA, 0x78, 0x16, 0xBF, 0x8F, 0x01, 0xCF, 0xEA, 0x41, 0x41, 0x40,
  0xDE, 0x5D, 0xAE, 0x22, 0x23, 0xB0, 0x03, 0x61, 0xA3, 0x96, 0x17,
  0x7A, 0x9C, 0xB4, 0x10, 0xFF, 0x61, 0xF2, 0x00, 0x15, 0xAD
};

fs::path fixture_path(char const *name) { return fs::path("test_data") / "hash" / name; }

}  // namespace

TEST_CASE("sha256 computes known hash") {
  auto const digest{ envy::sha256(fixture_path("abc.txt")) };
  CHECK(digest == kExpectedSha256Abc);
}

TEST_CASE("sha256 throws for missing file") {
  auto const missing{ fixture_path("does_not_exist.txt") };
  CHECK_FALSE(fs::exists(missing));
  CHECK_THROWS(envy::sha256(missing));
}

TEST_CASE("sha256_verify succeeds with correct lowercase hex") {
  std::string const expected_hex = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
  CHECK_NOTHROW(envy::sha256_verify(expected_hex, kExpectedSha256Abc));
}

TEST_CASE("sha256_verify succeeds with correct uppercase hex") {
  std::string const expected_hex = "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD";
  CHECK_NOTHROW(envy::sha256_verify(expected_hex, kExpectedSha256Abc));
}

TEST_CASE("sha256_verify succeeds with correct mixed case hex") {
  std::string const expected_hex = "Ba7816BF8f01CfEa414140dE5dAe2223B00361a396177A9Cb410FF61f20015Ad";
  CHECK_NOTHROW(envy::sha256_verify(expected_hex, kExpectedSha256Abc));
}

TEST_CASE("sha256_verify throws on mismatch") {
  std::string const wrong_hex = "0000000000000000000000000000000000000000000000000000000000000000";
  CHECK_THROWS_WITH(envy::sha256_verify(wrong_hex, kExpectedSha256Abc),
                    "SHA256 mismatch: expected 0000000000000000000000000000000000000000000000000000000000000000 but got ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST_CASE("sha256_verify throws on wrong length") {
  std::string const short_hex = "ba7816bf8f01cfea";
  CHECK_THROWS_WITH(envy::sha256_verify(short_hex, kExpectedSha256Abc),
                    "sha256_verify: expected hex string must be 64 characters, got 16");
}

TEST_CASE("sha256_verify throws on invalid hex character") {
  std::string const invalid_hex = "ga7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
  CHECK_THROWS_WITH(envy::sha256_verify(invalid_hex, kExpectedSha256Abc),
                    "sha256_verify: invalid hex character: g");
}
