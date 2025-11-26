#include "recipe_key.h"
#include "recipe_spec.h"

#include "sol/sol.hpp"

#include "doctest.h"

#include <unordered_set>

namespace envy {

TEST_CASE("recipe_key: canonical form from identity string") {
  recipe_key key("local.python@r4");

  CHECK(key.canonical() == "local.python@r4");
  CHECK(key.identity() == "local.python@r4");
  CHECK(key.namespace_() == "local");
  CHECK(key.name() == "python");
  CHECK(key.revision() == "@r4");
}

TEST_CASE("recipe_key: canonical form with options") {
  recipe_key key("local.python@r4{version=3.14}");

  CHECK(key.canonical() == "local.python@r4{version=3.14}");
  CHECK(key.identity() == "local.python@r4");
  CHECK(key.namespace_() == "local");
  CHECK(key.name() == "python");
  CHECK(key.revision() == "@r4");
}

TEST_CASE("recipe_key: canonical form with multiple options") {
  recipe_key key("foo.bar@r1{opt1=val1,opt2=val2}");

  CHECK(key.canonical() == "foo.bar@r1{opt1=val1,opt2=val2}");
  CHECK(key.identity() == "foo.bar@r1");
  CHECK(key.namespace_() == "foo");
  CHECK(key.name() == "bar");
  CHECK(key.revision() == "@r1");
}

TEST_CASE("recipe_key: from recipe_spec with no options") {
  recipe_spec spec;
  spec.identity = "local.ninja@r2";

  recipe_key key(spec);

  CHECK(key.canonical() == "local.ninja@r2");
  CHECK(key.identity() == "local.ninja@r2");
  CHECK(key.namespace_() == "local");
  CHECK(key.name() == "ninja");
  CHECK(key.revision() == "@r2");
}

TEST_CASE("recipe_key: from recipe_spec with options") {
  recipe_spec spec;
  spec.identity = "local.python@r4";
  spec.serialized_options = "{arch=\"arm64\",version=\"3.14\"}";

  recipe_key key(spec);

  // Options should be sorted in canonical form (strings are quoted)
  CHECK(key.canonical() == "local.python@r4{arch=\"arm64\",version=\"3.14\"}");
  CHECK(key.identity() == "local.python@r4");
  CHECK(key.namespace_() == "local");
  CHECK(key.name() == "python");
  CHECK(key.revision() == "@r4");
}

TEST_CASE("recipe_key: invalid identity - missing namespace") {
  CHECK_THROWS_WITH(recipe_key("python@r4"),
                    "Invalid identity (missing namespace): python@r4");
}

TEST_CASE("recipe_key: invalid identity - missing revision") {
  CHECK_THROWS_WITH(recipe_key("local.python"),
                    "Invalid identity (missing revision): local.python");
}

TEST_CASE("recipe_key: matching - exact canonical") {
  recipe_key key("local.python@r4{version=3.14}");

  CHECK(key.matches("local.python@r4{version=3.14}"));
}

TEST_CASE("recipe_key: matching - exact identity") {
  recipe_key key("local.python@r4{version=3.14}");

  CHECK(key.matches("local.python@r4"));
}

TEST_CASE("recipe_key: matching - name only") {
  recipe_key key("local.python@r4{version=3.14}");

  CHECK(key.matches("python"));
}

TEST_CASE("recipe_key: matching - namespace.name") {
  recipe_key key("local.python@r4{version=3.14}");

  CHECK(key.matches("local.python"));
}

TEST_CASE("recipe_key: matching - name@revision (any namespace)") {
  recipe_key key("local.python@r4{version=3.14}");

  CHECK(key.matches("python@r4"));
}

TEST_CASE("recipe_key: matching - different name doesn't match") {
  recipe_key key("local.python@r4");

  CHECK_FALSE(key.matches("ruby"));
  CHECK_FALSE(key.matches("local.ruby"));
  CHECK_FALSE(key.matches("ruby@r4"));
}

TEST_CASE("recipe_key: matching - different namespace doesn't match") {
  recipe_key key("local.python@r4");

  CHECK_FALSE(key.matches("foo.python"));
  CHECK_FALSE(key.matches("foo.python@r4"));
}

TEST_CASE("recipe_key: matching - different revision doesn't match") {
  recipe_key key("local.python@r4");

  CHECK_FALSE(key.matches("python@r3"));
  CHECK_FALSE(key.matches("local.python@r3"));
}

TEST_CASE("recipe_key: matching - multiple keys with same name") {
  recipe_key key1("local.ninja@r2");
  recipe_key key2("vendor.ninja@r1");

  // Both match name-only query
  CHECK(key1.matches("ninja"));
  CHECK(key2.matches("ninja"));

  // Only key1 matches namespace.name
  CHECK(key1.matches("local.ninja"));
  CHECK_FALSE(key2.matches("local.ninja"));

  // Only key2 matches vendor.ninja
  CHECK(key2.matches("vendor.ninja"));
  CHECK_FALSE(key1.matches("vendor.ninja"));
}

TEST_CASE("recipe_key: equality - same canonical") {
  recipe_key key1("local.python@r4{version=3.14}");
  recipe_key key2("local.python@r4{version=3.14}");

  CHECK(key1 == key2);
}

TEST_CASE("recipe_key: equality - different options") {
  recipe_key key1("local.python@r4{version=3.14}");
  recipe_key key2("local.python@r4{version=3.13}");

  CHECK_FALSE(key1 == key2);
}

TEST_CASE("recipe_key: equality - identity vs canonical") {
  recipe_key key1("local.python@r4");
  recipe_key key2("local.python@r4{version=3.14}");

  CHECK_FALSE(key1 == key2);
}

TEST_CASE("recipe_key: hash consistency") {
  recipe_key key1("local.python@r4{version=3.14}");
  recipe_key key2("local.python@r4{version=3.14}");

  CHECK(key1.hash() == key2.hash());
}

TEST_CASE("recipe_key: hash differs for different keys") {
  recipe_key key1("local.python@r4{version=3.14}");
  recipe_key key2("local.python@r4{version=3.13}");

  // Hash should differ (not guaranteed by hash contract, but likely)
  CHECK(key1.hash() != key2.hash());
}

TEST_CASE("recipe_key: usable in unordered_set") {
  std::unordered_set<recipe_key> set;

  recipe_key key1("local.python@r4{version=3.14}");
  recipe_key key2("local.python@r4{version=3.13}");
  recipe_key key3("local.python@r4{version=3.14}");  // Duplicate of key1

  set.insert(key1);
  set.insert(key2);
  set.insert(key3);

  // Should have 2 unique keys (key1 and key2, key3 is duplicate)
  CHECK(set.size() == 2);
  CHECK(set.contains(key1));
  CHECK(set.contains(key2));
  CHECK(set.contains(key3));  // Same as key1
}

TEST_CASE("recipe_key: ordering") {
  recipe_key key1("local.python@r4");
  recipe_key key2("local.ruby@r3");
  recipe_key key3("vendor.python@r4");

  // Lexicographic ordering
  CHECK(key1 < key2);  // "local.python" < "local.ruby"
  CHECK(key1 < key3);  // "local.python" < "vendor.python"
  CHECK(key2 < key3);  // "local.ruby" < "vendor.python"
}

TEST_CASE("recipe_key: complex namespace") {
  recipe_key key("com.example.foo@r1");

  CHECK(key.namespace_() == "com");
  CHECK(key.name() == "example.foo");  // Everything after first '.' and before '@'
  CHECK(key.revision() == "@r1");
}

TEST_CASE("recipe_key: matching with complex namespace") {
  recipe_key key("com.example.foo@r1");

  // "example.foo" is ambiguous - could be namespace.name or just name with dot
  // Current implementation treats it as namespace.name, so it won't match
  CHECK_FALSE(key.matches("example.foo"));
  CHECK(key.matches("com.example.foo"));       // namespace.name matches
  CHECK_FALSE(key.matches("example.foo@r1"));  // name@revision won't match
  CHECK(key.matches("com.example.foo@r1"));    // full identity matches
}

TEST_CASE("recipe_key: version with multiple @ symbols") {
  // Revision includes everything after first '@'
  recipe_key key("local.python@r4@special");

  CHECK(key.revision() == "@r4@special");
  CHECK(key.matches("python@r4@special"));
}

}  // namespace envy
