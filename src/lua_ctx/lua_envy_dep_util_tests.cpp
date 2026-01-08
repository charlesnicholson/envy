#include "lua_envy_dep_util.h"

#include "doctest.h"

namespace envy {

TEST_CASE("identity_matches: exact match") {
  CHECK(identity_matches("local.python@r4", "local.python@r4"));
  CHECK(identity_matches("acme.gcc@v2", "acme.gcc@v2"));
}

TEST_CASE("identity_matches: fuzzy match - name only") {
  CHECK(identity_matches("local.python@r4", "python"));
  CHECK(identity_matches("acme.gcc@v2", "gcc"));
  CHECK(identity_matches("vendor.toolchain-helpers@v1", "toolchain-helpers"));
}

TEST_CASE("identity_matches: fuzzy match - namespace.name") {
  CHECK(identity_matches("local.python@r4", "local.python"));
  CHECK(identity_matches("acme.gcc@v2", "acme.gcc"));
}

TEST_CASE("identity_matches: fuzzy match - name@revision") {
  CHECK(identity_matches("local.python@r4", "python@r4"));
  CHECK(identity_matches("acme.gcc@v2", "gcc@v2"));
}

TEST_CASE("identity_matches: no match - different name") {
  CHECK_FALSE(identity_matches("local.python@r4", "ruby"));
  CHECK_FALSE(identity_matches("acme.gcc@v2", "clang"));
}

TEST_CASE("identity_matches: no match - different namespace") {
  CHECK_FALSE(identity_matches("local.python@r4", "vendor.python"));
  CHECK_FALSE(identity_matches("local.python@r4", "vendor.python@r4"));
}

TEST_CASE("identity_matches: no match - different revision") {
  CHECK_FALSE(identity_matches("local.python@r4", "python@r3"));
  CHECK_FALSE(identity_matches("local.python@r4", "local.python@r3"));
}

TEST_CASE("identity_matches: hyphenated names") {
  CHECK(identity_matches("acme.toolchain-helpers@v1", "toolchain-helpers"));
  CHECK(identity_matches("acme.toolchain-helpers@v1", "acme.toolchain-helpers"));
  CHECK(identity_matches("acme.toolchain-helpers@v1", "toolchain-helpers@v1"));
}

TEST_CASE("identity_matches: complex namespace") {
  CHECK(identity_matches("com.example.foo@r1", "com.example.foo"));
  CHECK(identity_matches("com.example.foo@r1", "com.example.foo@r1"));
  // "example.foo" alone won't match because it's treated as namespace.name
  CHECK_FALSE(identity_matches("com.example.foo@r1", "example.foo"));
}

}  // namespace envy
