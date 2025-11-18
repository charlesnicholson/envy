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
  spec1.options = {{"version", lua_value(std::string("3.13"))}};

  recipe_spec spec2 = make_test_spec("local.python@r4");
  spec2.options = {{"version", lua_value(std::string("3.14"))}};

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

TEST_CASE("engine: register_alias and find by alias") {
  std::filesystem::path temp_dir = std::filesystem::temp_directory_path() / "envy_test_cache";
  cache c{temp_dir};
  engine eng{c, std::nullopt};

  recipe_spec spec = make_test_spec("local.python@r4");
  recipe *r = eng.ensure_recipe(spec);

  recipe_key key(spec);
  eng.register_alias("python", key);

  auto matches = eng.find_matches("python");
  CHECK(matches.size() == 1);
  CHECK(matches[0] == r);
}

TEST_CASE("engine: duplicate alias throws") {
  std::filesystem::path temp_dir = std::filesystem::temp_directory_path() / "envy_test_cache";
  cache c{temp_dir};
  engine eng{c, std::nullopt};

  recipe_spec spec1 = make_test_spec("local.python@r4");
  recipe_spec spec2 = make_test_spec("local.python@r5");

  eng.ensure_recipe(spec1);
  eng.ensure_recipe(spec2);

  recipe_key key1(spec1);
  recipe_key key2(spec2);

  eng.register_alias("python", key1);
  CHECK_THROWS_WITH(eng.register_alias("python", key2),
                    "Alias already registered: python");
}

TEST_CASE("engine: alias for non-existent recipe throws") {
  std::filesystem::path temp_dir = std::filesystem::temp_directory_path() / "envy_test_cache";
  cache c{temp_dir};
  engine eng{c, std::nullopt};

  recipe_key key("local.python@r4");
  CHECK_THROWS_AS(eng.register_alias("python", key), std::runtime_error);
}

}  // namespace envy
