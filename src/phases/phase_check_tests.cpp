#include "phase_check.h"

#include "cache.h"
#include "engine.h"
#include "lua_envy.h"
#include "recipe.h"
#include "recipe_spec.h"
#include "sol_util.h"

#include "doctest.h"

#include <memory>

namespace envy {

// Extern declarations for unit testing (not in public API)
extern bool recipe_has_check_verb(recipe *r, sol::state_view lua);
extern bool run_check_verb(recipe *r, engine &eng, sol::state_view lua);
extern bool run_check_string(recipe *r, engine &eng, std::string_view check_cmd);
extern bool run_check_function(recipe *r,
                               sol::state_view lua,
                               sol::protected_function check_func);

namespace {

// Helper fixture for creating test recipes with Lua states
struct test_recipe_fixture {
  recipe_spec *spec;
  std::unique_ptr<recipe> r;

  test_recipe_fixture() {
    spec = recipe_spec::pool()->emplace("test.package@v1",
                                        recipe_spec::weak_ref{},
                                        "{}",
                                        std::nullopt,
                                        nullptr,
                                        nullptr,
                                        std::vector<recipe_spec *>{},
                                        std::nullopt,
                                        std::filesystem::path{});

    // Create Lua state first
    auto lua_state = sol_util_make_lua_state();
    lua_envy_install(*lua_state);

    r = std::unique_ptr<recipe>(new recipe{
        .key = recipe_key(*spec),
        .spec = spec,
        .lua = std::move(lua_state),
        // lua_mutex is default-initialized
        .lock = nullptr,
        .declared_dependencies = {},
        .owned_dependency_specs = {},
        .dependencies = {},
        .canonical_identity_hash = {},
        .asset_path = std::filesystem::path{},
        .result_hash = {},
        .cache_ptr = nullptr,
        .default_shell_ptr = nullptr,
    });
  }

  void set_check_string(std::string_view cmd) { (*r->lua)["check"] = std::string(cmd); }

  void set_check_function(std::string_view lua_code) {
    std::string code = "return " + std::string(lua_code);
    sol::protected_function_result res{
      r->lua->safe_script(code, sol::script_pass_on_error)
    };
    if (!res.valid()) {
      sol::error err{ res };
      throw std::runtime_error(std::string("Failed to set check function: ") + err.what());
    }
    (*r->lua)["check"] = res.get<sol::protected_function>();
  }

  void clear_check() { (*r->lua)["check"] = sol::lua_nil; }
};

}  // namespace

// ============================================================================
// recipe_has_check_verb() tests
// ============================================================================

TEST_CASE("recipe_has_check_verb detects string check") {
  test_recipe_fixture f;
  f.set_check_string("true");

  bool has_check = recipe_has_check_verb(f.r.get(), sol::state_view{ *f.r->lua });
  CHECK(has_check);
}

TEST_CASE("recipe_has_check_verb detects function check") {
  test_recipe_fixture f;
  f.set_check_function("function(ctx) return true end");

  bool has_check = recipe_has_check_verb(f.r.get(), sol::state_view{ *f.r->lua });
  CHECK(has_check);
}

TEST_CASE("recipe_has_check_verb returns false when no check verb") {
  test_recipe_fixture f;
  // No check verb set

  bool has_check = recipe_has_check_verb(f.r.get(), sol::state_view{ *f.r->lua });
  CHECK_FALSE(has_check);
}

TEST_CASE("recipe_has_check_verb returns false for number") {
  test_recipe_fixture f;
  (*f.r->lua)["check"] = 42;

  bool has_check{ recipe_has_check_verb(f.r.get(), sol::state_view{ *f.r->lua }) };
  CHECK_FALSE(has_check);
}

TEST_CASE("recipe_has_check_verb returns false for invalid check type (table)") {
  test_recipe_fixture f;
  (*f.r->lua)["check"] = f.r->lua->create_table();

  bool has_check = recipe_has_check_verb(f.r.get(), sol::state_view{ *f.r->lua });
  CHECK_FALSE(has_check);
}

// ============================================================================
// run_check_string() tests
// ============================================================================

TEST_CASE("run_check_string returns true when command exits 0") {
  test_recipe_fixture f;

  cache test_cache;
  engine eng{ test_cache, std::nullopt };

  bool result = run_check_string(f.r.get(), eng, "exit 0");
  CHECK(result);
}

TEST_CASE("run_check_string returns false when command exits 1") {
  test_recipe_fixture f;

  cache test_cache;
  engine eng{ test_cache, std::nullopt };

  bool result = run_check_string(f.r.get(), eng, "exit 1");
  CHECK_FALSE(result);
}

TEST_CASE("run_check_string returns false when command exits non-zero") {
  test_recipe_fixture f;

  cache test_cache;
  engine eng{ test_cache, std::nullopt };

  bool result = run_check_string(f.r.get(), eng, "exit 42");
  CHECK_FALSE(result);
}

TEST_CASE("run_check_string returns true for successful command") {
  test_recipe_fixture f;

  cache test_cache;
  engine eng{ test_cache, std::nullopt };

#ifdef _WIN32
  bool result = run_check_string(f.r.get(), eng, "echo hello > nul");
#else
  bool result = run_check_string(f.r.get(), eng, "echo hello > /dev/null");
#endif
  CHECK(result);
}

TEST_CASE("run_check_string returns false for failing command") {
  test_recipe_fixture f;

  cache test_cache;
  engine eng{ test_cache, std::nullopt };

#ifdef _WIN32
  // PowerShell: exit with non-zero code
  bool result = run_check_string(f.r.get(), eng, "exit 1");
#else
  bool result = run_check_string(f.r.get(), eng, "false");
#endif
  CHECK_FALSE(result);
}

// ============================================================================
// run_check_function() tests
// ============================================================================

TEST_CASE("run_check_function returns true when function returns true") {
  test_recipe_fixture f;

  sol::protected_function_result res{
    f.r->lua->safe_script("return function(ctx) return true end", sol::script_pass_on_error)
  };
  REQUIRE(res.valid());
  sol::protected_function check_func{ res };

  bool result{ run_check_function(f.r.get(), sol::state_view{ *f.r->lua }, check_func) };
  CHECK(result);
}

TEST_CASE("run_check_function returns false when function returns false") {
  test_recipe_fixture f;

  sol::protected_function_result res{
    f.r->lua->safe_script("return function(ctx) return false end", sol::script_pass_on_error)
  };
  REQUIRE(res.valid());
  sol::protected_function check_func{ res };

  bool result{ run_check_function(f.r.get(), sol::state_view{ *f.r->lua }, check_func) };
  CHECK_FALSE(result);
}

TEST_CASE("run_check_function returns false when function returns nil") {
  test_recipe_fixture f;

  sol::protected_function_result res{
    f.r->lua->safe_script("return function(ctx) return nil end", sol::script_pass_on_error)
  };
  REQUIRE(res.valid());
  sol::protected_function check_func{ res };

  bool result{ run_check_function(f.r.get(), sol::state_view{ *f.r->lua }, check_func) };
  CHECK_FALSE(result);
}

TEST_CASE("run_check_function returns true when function returns truthy value") {
  test_recipe_fixture f;

  sol::protected_function_result res{
    f.r->lua->safe_script("return function(ctx) return 42 end", sol::script_pass_on_error)
  };
  REQUIRE(res.valid());
  sol::protected_function check_func{ res };

  bool result{ run_check_function(f.r.get(), sol::state_view{ *f.r->lua }, check_func) };
  CHECK(result);
}

TEST_CASE("run_check_function returns true when function returns string") {
  test_recipe_fixture f;

  sol::protected_function_result res{
    f.r->lua->safe_script("return function(ctx) return 'yes' end",
                          sol::script_pass_on_error)
  };
  REQUIRE(res.valid());
  sol::protected_function check_func{ res };

  bool result{ run_check_function(f.r.get(), sol::state_view{ *f.r->lua }, check_func) };
  CHECK(result);
}

TEST_CASE("run_check_function throws when function has Lua error") {
  test_recipe_fixture f;

  sol::protected_function_result res{
    f.r->lua->safe_script("return function(ctx) error('test error') end",
                          sol::script_pass_on_error)
  };
  REQUIRE(res.valid());
  sol::protected_function check_func{ res };

  CHECK_THROWS_AS(
      run_check_function(f.r.get(), sol::state_view{ *f.r->lua }, check_func),
      std::runtime_error);
}

TEST_CASE("run_check_function receives empty ctx table") {
  test_recipe_fixture f;

  sol::protected_function_result res{ f.r->lua->safe_script(
      R"(
        return function(ctx)
          return type(ctx) == 'table'
        end
      )",
      sol::script_pass_on_error) };
  REQUIRE(res.valid());
  sol::protected_function check_func{ res };

  bool result{ run_check_function(f.r.get(), sol::state_view{ *f.r->lua }, check_func) };
  CHECK(result);
}

// ============================================================================
// run_check_verb() tests - dispatch logic
// ============================================================================

TEST_CASE("run_check_verb dispatches to string handler") {
  test_recipe_fixture f;
  f.set_check_string("exit 0");

  cache test_cache;
  engine eng{ test_cache, std::nullopt };

  bool result = run_check_verb(f.r.get(), eng, sol::state_view{ *f.r->lua });
  CHECK(result);
}

TEST_CASE("run_check_verb dispatches to function handler") {
  test_recipe_fixture f;
  f.set_check_function("function(ctx) return true end");

  cache test_cache;
  engine eng{ test_cache, std::nullopt };

  bool result = run_check_verb(f.r.get(), eng, sol::state_view{ *f.r->lua });
  CHECK(result);
}

TEST_CASE("run_check_verb returns false when no check verb") {
  test_recipe_fixture f;
  // No check verb set

  cache test_cache;
  engine eng{ test_cache, std::nullopt };

  bool result = run_check_verb(f.r.get(), eng, sol::state_view{ *f.r->lua });
  CHECK_FALSE(result);
}

TEST_CASE("run_check_verb returns false for table check type") {
  test_recipe_fixture f;
  (*f.r->lua)["check"] = f.r->lua->create_table();

  cache test_cache;
  engine eng{ test_cache, std::nullopt };

  // Tables are not functions or strings, so check verb is not present
  bool result = run_check_verb(f.r.get(), eng, sol::state_view{ *f.r->lua });
  CHECK_FALSE(result);
}

TEST_CASE("run_check_verb string check respects exit code") {
  test_recipe_fixture f;

  cache test_cache;
  engine eng{ test_cache, std::nullopt };

  SUBCASE("exit 0 returns true") {
    f.set_check_string("exit 0");
    bool result = run_check_verb(f.r.get(), eng, sol::state_view{ *f.r->lua });
    CHECK(result);
  }

  SUBCASE("exit 1 returns false") {
    f.set_check_string("exit 1");
    bool result = run_check_verb(f.r.get(), eng, sol::state_view{ *f.r->lua });
    CHECK_FALSE(result);
  }
}

TEST_CASE("run_check_verb function check respects return value") {
  test_recipe_fixture f;

  cache test_cache;
  engine eng{ test_cache, std::nullopt };

  SUBCASE("function returns true") {
    f.set_check_function("function(ctx) return true end");
    bool result = run_check_verb(f.r.get(), eng, sol::state_view{ *f.r->lua });
    CHECK(result);
  }

  SUBCASE("function returns false") {
    f.set_check_function("function(ctx) return false end");
    bool result = run_check_verb(f.r.get(), eng, sol::state_view{ *f.r->lua });
    CHECK_FALSE(result);
  }
}

// ============================================================================
// Error handling tests
// ============================================================================

TEST_CASE("run_check_function propagates Lua error with context") {
  test_recipe_fixture f;
  f.spec->identity = "my.package@v1";

  sol::protected_function_result res{ f.r->lua->safe_script(
      "return function(ctx) error('something went wrong') end", sol::script_pass_on_error) };
  REQUIRE(res.valid());

  sol::protected_function check_func{ res };

  try {
    run_check_function(f.r.get(), sol::state_view{ *f.r->lua }, check_func);
    FAIL("Expected exception");
  } catch (std::runtime_error const &e) {
    std::string msg{ e.what() };
    CHECK(msg.find("my.package@v1") != std::string::npos);
    CHECK(msg.find("something went wrong") != std::string::npos);
  }
}

}  // namespace envy
