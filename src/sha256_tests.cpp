#include "sha256.h"

#include "doctest.h"

#include <array>
#include <filesystem>

namespace fs = std::filesystem;

namespace {

constexpr std::array<unsigned char, 32> kExpectedSha256Abc{
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
