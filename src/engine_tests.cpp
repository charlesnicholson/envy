#include "engine.h"

#include "cache.h"
#include "recipe.h"

#include "doctest.h"

#include <filesystem>

namespace envy {

static recipe_spec make_test_spec(std::string const &identity) {
  recipe_spec spec;
  spec.identity = identity;
  spec.source = recipe_spec::local_source{};
  return spec;
}

TEST_CASE("engine: ensure_recipe creates new recipe") {
  std::filesystem::path temp_dir = std::filesystem::temp_directory_path() / "envy_test_cache";
  cache c{temp_dir};
  engine eng{c, std::nullopt};

  recipe_spec spec = make_test_spec("local.python@r4");
  recipe *r = eng.ensure_recipe(spec);

  CHECK(r != nullptr);
  CHECK(r->key->canonical() == "local.python@r4");
}

TEST_CASE("engine: ensure_recipe is memoized") {
  std::filesystem::path temp_dir = std::filesystem::temp_directory_path() / "envy_test_cache";
  cache c{temp_dir};
  engine eng{c, std::nullopt};

  recipe_spec spec = make_test_spec("local.python@r4");
  recipe *r1 = eng.ensure_recipe(spec);
  recipe *r2 = eng.ensure_recipe(spec);

  CHECK(r1 == r2);
}

TEST_CASE("engine: different options create different recipes") {
  std::filesystem::path temp_dir = std::filesystem::temp_directory_path() / "envy_test_cache";
  cache c{temp_dir};
  engine eng{c, std::nullopt};

  recipe_spec spec1 = make_test_spec("local.python@r4");
  spec1.serialized_options = R"({version="3.13"})";

  recipe_spec spec2 = make_test_spec("local.python@r4");
  spec2.serialized_options = R"({version="3.14"})";

  recipe *r1 = eng.ensure_recipe(spec1);
  recipe *r2 = eng.ensure_recipe(spec2);

  CHECK(r1 != r2);
}

TEST_CASE("engine: find_exact returns recipe") {
  std::filesystem::path temp_dir = std::filesystem::temp_directory_path() / "envy_test_cache";
  cache c{temp_dir};
  engine eng{c, std::nullopt};

  recipe_spec spec = make_test_spec("local.python@r4");
  recipe *r = eng.ensure_recipe(spec);

  recipe_key key(spec);
  CHECK(eng.find_exact(key) == r);
}

TEST_CASE("engine: find_exact returns nullptr for non-existent") {
  std::filesystem::path temp_dir = std::filesystem::temp_directory_path() / "envy_test_cache";
  cache c{temp_dir};
  engine eng{c, std::nullopt};

  recipe_key key("local.python@r4");
  CHECK(eng.find_exact(key) == nullptr);
}

TEST_CASE("engine: find_matches by name") {
  std::filesystem::path temp_dir = std::filesystem::temp_directory_path() / "envy_test_cache";
  cache c{temp_dir};
  engine eng{c, std::nullopt};

  eng.ensure_recipe(make_test_spec("local.python@r4"));
  eng.ensure_recipe(make_test_spec("vendor.python@r3"));
  eng.ensure_recipe(make_test_spec("local.ruby@r2"));

  auto matches = eng.find_matches("python");
  CHECK(matches.size() == 2);
}

TEST_CASE("engine: find_matches by namespace.name") {
  std::filesystem::path temp_dir = std::filesystem::temp_directory_path() / "envy_test_cache";
  cache c{temp_dir};
  engine eng{c, std::nullopt};

  eng.ensure_recipe(make_test_spec("local.python@r4"));
  eng.ensure_recipe(make_test_spec("vendor.python@r3"));
  eng.ensure_recipe(make_test_spec("local.python@r2"));

  auto matches = eng.find_matches("local.python");
  CHECK(matches.size() == 2);
}

TEST_CASE("engine: find_matches with no matches") {
  std::filesystem::path temp_dir = std::filesystem::temp_directory_path() / "envy_test_cache";
  cache c{temp_dir};
  engine eng{c, std::nullopt};

  eng.ensure_recipe(make_test_spec("local.python@r4"));

  auto matches = eng.find_matches("ruby");
  CHECK(matches.empty());
}

}  // namespace envy
