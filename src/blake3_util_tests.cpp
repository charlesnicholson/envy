#include "blake3_util.h"

#include "doctest.h"

#include <string>

namespace {

// Known BLAKE3 hash of "abc"
constexpr envy::blake3_t kExpectedBlake3Abc{ 0x64, 0x37, 0xb3, 0xac, 0x38, 0x46, 0x51,
                                             0x33, 0xff, 0xb6, 0x3b, 0x75, 0x27, 0x3a,
                                             0x8d, 0xb5, 0x48, 0xc5, 0x58, 0x46, 0x5d,
                                             0x79, 0xdb, 0x03, 0xfd, 0x35, 0x9c, 0x6c,
                                             0xd5, 0xbd, 0x9d, 0x85u };

}  // namespace

TEST_CASE("blake3_hash computes known hash") {
  std::string constexpr input{ "abc" };
  auto const digest{ envy::blake3_hash(input.data(), input.size()) };
  CHECK(digest == kExpectedBlake3Abc);
}

TEST_CASE("blake3_hash is deterministic") {
  std::string constexpr input{ "test input" };
  auto const digest1{ envy::blake3_hash(input.data(), input.size()) };
  auto const digest2{ envy::blake3_hash(input.data(), input.size()) };
  CHECK(digest1 == digest2);
}

TEST_CASE("blake3_hash handles empty input") {
  // Known BLAKE3 hash of empty string
  constexpr envy::blake3_t kExpectedEmpty{
    0xaf, 0x13, 0x49, 0xb9, 0xf5, 0xf9, 0xa1, 0xa6, 0xa0, 0x40, 0x4d,
    0xea, 0x36, 0xdc, 0xc9, 0x49, 0x9b, 0xcb, 0x25, 0xc9, 0xad, 0xc1,
    0x12, 0xb7, 0xcc, 0x9a, 0x93, 0xca, 0xe4, 0x1f, 0x32, 0x62u
  };

  CHECK(envy::blake3_hash("", 0) == kExpectedEmpty);
}

TEST_CASE("blake3_hash different inputs produce different outputs") {
  std::string const input1{ "hello" };
  std::string const input2{ "world" };

  auto const digest1{ envy::blake3_hash(input1.data(), input1.size()) };
  auto const digest2{ envy::blake3_hash(input2.data(), input2.size()) };
  CHECK(digest1 != digest2);
}
