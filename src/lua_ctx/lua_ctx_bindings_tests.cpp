#include "engine.h"
#include "lua_ctx_bindings.h"

#include "doctest.h"

// NOTE: Tests disabled during graph redesign (2025-11-13)
// The new architecture removed is_transitive_dependency() in favor of
// is_declared_dependency() which only checks direct dependencies.
// Graph topology enforces synchronization instead of transitive dependency tracking.
// These tests are obsolete and need to be rewritten or removed.

#if 0

namespace {

// Helper to create a minimal graph_state for testing
struct test_graph_state {
  // tbb::flow::graph graph;  // Removed - TBB no longer in codebase
  envy::cache cache_;
  // envy::graph_state state;  // Removed - graph_state obsolete

  test_graph_state() : cache_(std::nullopt) {}  // , state{ graph, cache_, nullptr } {}

  // Add a spec with given identity and dependencies
  void add_pkg(std::string const &identity,
                  std::vector<std::string> const &dependencies = {}) {
    typename decltype(state.specs)::accessor acc;
    if (state.specs.insert(acc, identity)) {
      acc->second.identity = identity;
      acc->second.declared_dependencies = dependencies;
    }
  }
};

}  // namespace

TEST_CASE("is_transitive_dependency - direct dependency") {
  test_graph_state tgs;
  tgs.add_pkg("A", { "B" });
  tgs.add_pkg("B", {});

  CHECK(envy::is_transitive_dependency(&tgs.state, "A", "B") == true);
  CHECK(envy::is_transitive_dependency(&tgs.state, "A", "C") == false);
}

TEST_CASE("is_transitive_dependency - self reference") {
  test_graph_state tgs;
  tgs.add_pkg("A", {});

  CHECK(envy::is_transitive_dependency(&tgs.state, "A", "A") == true);
}

TEST_CASE("is_transitive_dependency - transitive 2 levels") {
  test_graph_state tgs;
  tgs.add_pkg("A", { "B" });
  tgs.add_pkg("B", { "C" });
  tgs.add_pkg("C", {});

  // A → B → C: A can access C transitively
  CHECK(envy::is_transitive_dependency(&tgs.state, "A", "C") == true);
  CHECK(envy::is_transitive_dependency(&tgs.state, "B", "C") == true);
  CHECK(envy::is_transitive_dependency(&tgs.state, "C", "A") == false);
}

TEST_CASE("is_transitive_dependency - transitive 3 levels") {
  test_graph_state tgs;
  tgs.add_pkg("A", { "B" });
  tgs.add_pkg("B", { "C" });
  tgs.add_pkg("C", { "D" });
  tgs.add_pkg("D", {});

  // A → B → C → D: A can access D transitively
  CHECK(envy::is_transitive_dependency(&tgs.state, "A", "D") == true);
  CHECK(envy::is_transitive_dependency(&tgs.state, "B", "D") == true);
  CHECK(envy::is_transitive_dependency(&tgs.state, "C", "D") == true);
  CHECK(envy::is_transitive_dependency(&tgs.state, "D", "A") == false);
}

TEST_CASE("is_transitive_dependency - diamond dependency") {
  test_graph_state tgs;
  tgs.add_pkg("A", { "B", "C" });
  tgs.add_pkg("B", { "D" });
  tgs.add_pkg("C", { "D" });
  tgs.add_pkg("D", {});

  // A → B → D and A → C → D: A can access D via both paths
  CHECK(envy::is_transitive_dependency(&tgs.state, "A", "D") == true);
  CHECK(envy::is_transitive_dependency(&tgs.state, "A", "B") == true);
  CHECK(envy::is_transitive_dependency(&tgs.state, "A", "C") == true);
}

TEST_CASE("is_transitive_dependency - non-dependency") {
  test_graph_state tgs;
  tgs.add_pkg("A", { "B" });
  tgs.add_pkg("B", {});
  tgs.add_pkg("C", {});

  // A → B, C is unrelated
  CHECK(envy::is_transitive_dependency(&tgs.state, "A", "C") == false);
  CHECK(envy::is_transitive_dependency(&tgs.state, "B", "C") == false);
  CHECK(envy::is_transitive_dependency(&tgs.state, "C", "A") == false);
}

TEST_CASE("is_transitive_dependency - circular dependencies") {
  test_graph_state tgs;
  tgs.add_pkg("A", { "B" });
  tgs.add_pkg("B", { "C" });
  tgs.add_pkg("C", { "A" });

  // A → B → C → A (cycle)
  // Each can access the others transitively
  CHECK(envy::is_transitive_dependency(&tgs.state, "A", "B") == true);
  CHECK(envy::is_transitive_dependency(&tgs.state, "A", "C") == true);
  CHECK(envy::is_transitive_dependency(&tgs.state, "B", "A") == true);
  CHECK(envy::is_transitive_dependency(&tgs.state, "B", "C") == true);
  CHECK(envy::is_transitive_dependency(&tgs.state, "C", "A") == true);
  CHECK(envy::is_transitive_dependency(&tgs.state, "C", "B") == true);

  // But none can access unrelated D
  tgs.add_pkg("D", {});
  CHECK(envy::is_transitive_dependency(&tgs.state, "A", "D") == false);
}

TEST_CASE("is_transitive_dependency - missing intermediate pkg") {
  test_graph_state tgs;
  tgs.add_pkg("A", { "B" });
  // B not added to graph
  tgs.add_pkg("C", {});

  // A → B → C, but B doesn't exist yet
  // Should return false (can't traverse through missing node)
  CHECK(envy::is_transitive_dependency(&tgs.state, "A", "C") == false);
}

TEST_CASE("is_transitive_dependency - missing target pkg") {
  test_graph_state tgs;
  tgs.add_pkg("A", { "B" });
  tgs.add_pkg("B", {});
  // C not added

  // A asks for C which doesn't exist in graph
  CHECK(envy::is_transitive_dependency(&tgs.state, "A", "C") == false);
}

TEST_CASE("is_transitive_dependency - empty dependency list") {
  test_graph_state tgs;
  tgs.add_pkg("A", {});
  tgs.add_pkg("B", {});

  // A has no deps, can't access B
  CHECK(envy::is_transitive_dependency(&tgs.state, "A", "B") == false);
}

TEST_CASE("is_transitive_dependency - multiple dependencies") {
  test_graph_state tgs;
  tgs.add_pkg("A", { "B", "C", "D" });
  tgs.add_pkg("B", {});
  tgs.add_pkg("C", {});
  tgs.add_pkg("D", {});

  // A directly depends on B, C, D
  CHECK(envy::is_transitive_dependency(&tgs.state, "A", "B") == true);
  CHECK(envy::is_transitive_dependency(&tgs.state, "A", "C") == true);
  CHECK(envy::is_transitive_dependency(&tgs.state, "A", "D") == true);
}

#endif  // Tests disabled
