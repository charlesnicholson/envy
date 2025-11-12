#include "util.h"

#include "doctest.h"

#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <variant>
#include <vector>

namespace {

std::filesystem::path make_temp_path(char const *tag) {
  static std::atomic<int> counter{ 0 };
  auto const id = counter.fetch_add(1, std::memory_order_relaxed);
  auto base{ std::filesystem::temp_directory_path() };
  return base / ("envy-util-test-" + std::string(tag) + "-" + std::to_string(id));
}

void write_dummy_file(std::filesystem::path const &path) {
  std::ofstream out{ path };
  out << "envy-test";
}

}  // namespace

TEST_CASE("match with std::variant of int and string") {
  using var_t = std::variant<int, std::string>;

  var_t v1{ 42 };
  var_t v2{ std::string("hello") };

  auto result1{ std::visit(
      envy::match{ [](int x) { return x * 2; },
                   [](std::string const &s) { return static_cast<int>(s.size()); } },
      v1) };

  auto result2{ std::visit(
      envy::match{ [](int x) { return x * 2; },
                   [](std::string const &s) { return static_cast<int>(s.size()); } },
      v2) };

  CHECK(result1 == 84);
  CHECK(result2 == 5);
}

TEST_CASE("match with different return types") {
  using var_t = std::variant<int, double>;

  var_t v1{ 42 };
  var_t v2{ 3.14 };

  auto result1{ std::visit(envy::match{ [](int x) { return std::to_string(x); },
                                        [](double d) { return std::to_string(d); } },
                           v1) };

  auto result2{ std::visit(envy::match{ [](int x) { return std::to_string(x); },
                                        [](double d) { return std::to_string(d); } },
                           v2) };

  CHECK(result1 == "42");
  CHECK(result2 == "3.140000");
}

TEST_CASE("match with three alternatives") {
  using var_t = std::variant<int, double, std::string>;

  var_t v1{ 42 };
  var_t v2{ 3.14 };
  var_t v3{ std::string("test") };

  auto visitor{ envy::match{ [](int x) { return 1; },
                             [](double) { return 2; },
                             [](std::string const &) { return 3; } } };

  CHECK(std::visit(visitor, v1) == 1);
  CHECK(std::visit(visitor, v2) == 2);
  CHECK(std::visit(visitor, v3) == 3);
}

TEST_CASE("match with capturing lambdas") {
  using var_t = std::variant<int, double>;

  int multiplier{ 10 };
  double divisor{ 2.0 };

  var_t v1{ 5 };
  var_t v2{ 10.0 };

  auto visitor{ envy::match{ [&](int x) { return x * multiplier; },
                             [&](double d) { return static_cast<int>(d / divisor); } } };

  CHECK(std::visit(visitor, v1) == 50);
  CHECK(std::visit(visitor, v2) == 5);
}

TEST_CASE("match with void return") {
  using var_t = std::variant<int, std::string>;

  int int_count{};
  int string_count{};

  var_t v1{ 42 };
  var_t v2{ std::string("test") };

  auto counter{ envy::match{ [&](int) { ++int_count; },
                             [&](std::string const &) { ++string_count; } } };

  std::visit(counter, v1);
  std::visit(counter, v2);
  std::visit(counter, v1);

  CHECK(int_count == 2);
  CHECK(string_count == 1);
}

// Hex conversion tests

TEST_CASE("util_bytes_to_hex converts empty input") {
  std::string result = envy::util_bytes_to_hex("", 0);
  CHECK(result.empty());
}

TEST_CASE("util_bytes_to_hex converts single byte") {
  unsigned char data[] = {0xab};
  std::string result = envy::util_bytes_to_hex(data, 1);
  CHECK(result == "ab");
}

TEST_CASE("util_bytes_to_hex converts multiple bytes") {
  unsigned char data[] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};
  std::string result = envy::util_bytes_to_hex(data, 8);
  CHECK(result == "0123456789abcdef");
}

TEST_CASE("util_bytes_to_hex produces lowercase") {
  unsigned char data[] = {0xff, 0xaa, 0xbb, 0xcc};
  std::string result = envy::util_bytes_to_hex(data, 4);
  CHECK(result == "ffaabbcc");
}

TEST_CASE("util_bytes_to_hex handles zero bytes") {
  unsigned char data[] = {0x00, 0x00, 0x00};
  std::string result = envy::util_bytes_to_hex(data, 3);
  CHECK(result == "000000");
}

TEST_CASE("util_bytes_to_hex handles all byte values") {
  unsigned char data[256];
  for (int i = 0; i < 256; ++i) {
    data[i] = static_cast<unsigned char>(i);
  }
  std::string result = envy::util_bytes_to_hex(data, 256);
  CHECK(result.size() == 512);
  // Spot check a few values
  CHECK(result.substr(0, 2) == "00");
  CHECK(result.substr(2, 2) == "01");
  CHECK(result.substr(254, 2) == "7f");
  CHECK(result.substr(510, 2) == "ff");
}

TEST_CASE("util_hex_to_bytes converts empty string") {
  auto result = envy::util_hex_to_bytes("");
  CHECK(result.empty());
}

TEST_CASE("util_hex_to_bytes converts single byte lowercase") {
  auto result = envy::util_hex_to_bytes("ab");
  REQUIRE(result.size() == 1);
  CHECK(result[0] == 0xab);
}

TEST_CASE("util_hex_to_bytes converts single byte uppercase") {
  auto result = envy::util_hex_to_bytes("AB");
  REQUIRE(result.size() == 1);
  CHECK(result[0] == 0xab);
}

TEST_CASE("util_hex_to_bytes converts single byte mixed case") {
  auto result = envy::util_hex_to_bytes("Ab");
  REQUIRE(result.size() == 1);
  CHECK(result[0] == 0xab);
}

TEST_CASE("util_hex_to_bytes converts multiple bytes") {
  auto result = envy::util_hex_to_bytes("0123456789abcdef");
  REQUIRE(result.size() == 8);
  CHECK(result[0] == 0x01);
  CHECK(result[1] == 0x23);
  CHECK(result[2] == 0x45);
  CHECK(result[3] == 0x67);
  CHECK(result[4] == 0x89);
  CHECK(result[5] == 0xab);
  CHECK(result[6] == 0xcd);
  CHECK(result[7] == 0xef);
}

TEST_CASE("util_hex_to_bytes handles zero bytes") {
  auto result = envy::util_hex_to_bytes("000000");
  REQUIRE(result.size() == 3);
  CHECK(result[0] == 0x00);
  CHECK(result[1] == 0x00);
  CHECK(result[2] == 0x00);
}

TEST_CASE("util_hex_to_bytes handles all uppercase") {
  auto result = envy::util_hex_to_bytes("FFAABBCC");
  REQUIRE(result.size() == 4);
  CHECK(result[0] == 0xff);
  CHECK(result[1] == 0xaa);
  CHECK(result[2] == 0xbb);
  CHECK(result[3] == 0xcc);
}

TEST_CASE("util_hex_to_bytes throws on odd length") {
  CHECK_THROWS_WITH(envy::util_hex_to_bytes("a"),
                    "util_hex_to_bytes: hex string must have even length, got 1");
  CHECK_THROWS_WITH(envy::util_hex_to_bytes("abc"),
                    "util_hex_to_bytes: hex string must have even length, got 3");
}

TEST_CASE("util_hex_to_bytes throws on invalid character") {
  CHECK_THROWS_WITH(envy::util_hex_to_bytes("ag"),
                    "util_hex_to_bytes: invalid character at position 1");
  CHECK_THROWS_WITH(envy::util_hex_to_bytes("0z"),
                    "util_hex_to_bytes: invalid character at position 1");
  CHECK_THROWS_WITH(envy::util_hex_to_bytes("!0"),
                    "util_hex_to_bytes: invalid character at position 0");
  CHECK_THROWS_WITH(envy::util_hex_to_bytes(" 0"),
                    "util_hex_to_bytes: invalid character at position 0");
}

TEST_CASE("util_bytes_to_hex and util_hex_to_bytes round-trip") {
  unsigned char original[] = {0x00, 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xff};
  std::string hex = envy::util_bytes_to_hex(original, 9);
  auto recovered = envy::util_hex_to_bytes(hex);
  REQUIRE(recovered.size() == 9);
  CHECK(std::memcmp(original, recovered.data(), 9) == 0);
}

TEST_CASE("util_hex_to_bytes and util_bytes_to_hex round-trip") {
  std::string original_hex = "0123456789abcdefABCDEF";
  auto bytes = envy::util_hex_to_bytes(original_hex);
  std::string recovered_hex = envy::util_bytes_to_hex(bytes.data(), bytes.size());
  // Result should be lowercase
  CHECK(recovered_hex == "0123456789abcdefabcdef");
}

TEST_CASE("scoped_path_cleanup removes file on destruction") {
  auto path = make_temp_path("cleanup");
  write_dummy_file(path);
  REQUIRE(std::filesystem::exists(path));
  {
    envy::scoped_path_cleanup cleanup{ path };
    CHECK(std::filesystem::exists(path));
  }
  CHECK_FALSE(std::filesystem::exists(path));
}

TEST_CASE("scoped_path_cleanup reset switches targets and cleans previous file") {
  auto first = make_temp_path("first");
  auto second = make_temp_path("second");
  write_dummy_file(first);
  write_dummy_file(second);
  REQUIRE(std::filesystem::exists(first));
  REQUIRE(std::filesystem::exists(second));

  {
    envy::scoped_path_cleanup cleanup{ first };
    CHECK(std::filesystem::exists(first));
    cleanup.reset(second);
    CHECK_FALSE(std::filesystem::exists(first));
    CHECK(std::filesystem::exists(second));
  }

  CHECK_FALSE(std::filesystem::exists(second));
}

TEST_CASE("util_load_file loads empty file") {
  auto path = make_temp_path("empty");
  std::ofstream{ path };  // Create empty file
  envy::scoped_path_cleanup cleanup{ path };

  auto data = envy::util_load_file(path);
  CHECK(data.empty());
}

TEST_CASE("util_load_file loads small text file") {
  auto path = make_temp_path("small");
  {
    std::ofstream out{ path, std::ios::binary };
    out << "hello world";
  }
  envy::scoped_path_cleanup cleanup{ path };

  auto data = envy::util_load_file(path);
  REQUIRE(data.size() == 11);
  CHECK(std::memcmp(data.data(), "hello world", 11) == 0);
}

TEST_CASE("util_load_file loads binary data") {
  auto path = make_temp_path("binary");
  unsigned char const test_data[] = {0x00, 0x01, 0x02, 0xff, 0xfe, 0xfd};
  {
    std::ofstream out{ path, std::ios::binary };
    out.write(reinterpret_cast<char const *>(test_data), sizeof(test_data));
  }
  envy::scoped_path_cleanup cleanup{ path };

  auto data = envy::util_load_file(path);
  REQUIRE(data.size() == 6);
  CHECK(data[0] == 0x00);
  CHECK(data[1] == 0x01);
  CHECK(data[2] == 0x02);
  CHECK(data[3] == 0xff);
  CHECK(data[4] == 0xfe);
  CHECK(data[5] == 0xfd);
}

TEST_CASE("util_load_file loads larger file") {
  auto path = make_temp_path("large");
  std::vector<unsigned char> test_data(10000);
  for (size_t i = 0; i < test_data.size(); ++i) {
    test_data[i] = static_cast<unsigned char>(i % 256);
  }
  {
    std::ofstream out{ path, std::ios::binary };
    out.write(reinterpret_cast<char const *>(test_data.data()), test_data.size());
  }
  envy::scoped_path_cleanup cleanup{ path };

  auto data = envy::util_load_file(path);
  REQUIRE(data.size() == 10000);
  CHECK(std::memcmp(data.data(), test_data.data(), 10000) == 0);
}

TEST_CASE("util_load_file throws on nonexistent file") {
  auto path = make_temp_path("nonexistent");
  CHECK_THROWS_WITH(envy::util_load_file(path), doctest::Contains("failed to open file"));
}

TEST_CASE("util_load_file handles files with null bytes") {
  auto path = make_temp_path("nullbytes");
  unsigned char const test_data[] = {'a', 'b', 0x00, 'c', 'd', 0x00, 0x00, 'e'};
  {
    std::ofstream out{ path, std::ios::binary };
    out.write(reinterpret_cast<char const *>(test_data), sizeof(test_data));
  }
  envy::scoped_path_cleanup cleanup{ path };

  auto data = envy::util_load_file(path);
  REQUIRE(data.size() == 8);
  CHECK(data[0] == 'a');
  CHECK(data[1] == 'b');
  CHECK(data[2] == 0x00);
  CHECK(data[3] == 'c');
  CHECK(data[4] == 'd');
  CHECK(data[5] == 0x00);
  CHECK(data[6] == 0x00);
  CHECK(data[7] == 'e');
}
