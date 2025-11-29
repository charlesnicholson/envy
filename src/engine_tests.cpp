#include "engine.h"

#include "doctest.h"

#include <vector>

namespace envy {

TEST_CASE("validate_dependency_cycle: no cycle") {
  std::vector<std::string> const ancestors{ "A", "B", "C" };
  CHECK_NOTHROW(validate_dependency_cycle("D", ancestors, "C", "Dependency"));
}

TEST_CASE("validate_dependency_cycle: direct self-loop") {
  std::vector<std::string> const ancestors{ "A", "B" };
  CHECK_THROWS_WITH(validate_dependency_cycle("C", ancestors, "C", "Dependency"),
                    "Dependency cycle detected: C -> C");
}

TEST_CASE("validate_dependency_cycle: cycle in ancestor chain") {
  std::vector<std::string> const ancestors{ "A", "B", "C" };
  CHECK_THROWS_WITH(validate_dependency_cycle("B", ancestors, "D", "Dependency"),
                    "Dependency cycle detected: B -> C -> D -> B");
}

TEST_CASE("validate_dependency_cycle: cycle at chain start") {
  std::vector<std::string> const ancestors{ "A", "B", "C" };
  CHECK_THROWS_WITH(validate_dependency_cycle("A", ancestors, "D", "Dependency"),
                    "Dependency cycle detected: A -> B -> C -> D -> A");
}

TEST_CASE("validate_dependency_cycle: fetch dependency error message") {
  std::vector<std::string> const ancestors{ "A", "B" };
  CHECK_THROWS_WITH(validate_dependency_cycle("A", ancestors, "C", "Fetch dependency"),
                    "Fetch dependency cycle detected: A -> B -> C -> A");
}

TEST_CASE("validate_dependency_cycle: empty ancestor chain with self-loop") {
  std::vector<std::string> const ancestors{};
  CHECK_THROWS_WITH(validate_dependency_cycle("A", ancestors, "A", "Dependency"),
                    "Dependency cycle detected: A -> A");
}

TEST_CASE("validate_dependency_cycle: empty ancestor chain without cycle") {
  std::vector<std::string> const ancestors{};
  CHECK_NOTHROW(validate_dependency_cycle("B", ancestors, "A", "Dependency"));
}

}  // namespace envy
