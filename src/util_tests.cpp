#include "util.h"

#include "doctest.h"

#include <string>
#include <variant>

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
