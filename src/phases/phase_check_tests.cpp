#include "phase_check.h"

#include "cache.h"
#include "engine.h"
#include "lua_envy.h"
#include "recipe.h"
#include "recipe_spec.h"

#include "doctest.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

#include <memory>

namespace envy {

// Extern declarations for unit testing (not in public API)
extern bool recipe_has_check_verb(recipe *r, lua_State *lua);
extern bool run_check_verb(recipe *r, engine &eng, lua_State *lua);
extern bool run_check_string(recipe *r, engine &eng, std::string_view check_cmd);
extern bool run_check_function(recipe *r, lua_State *lua, sol::protected_function check_func);

namespace {

// Helper fixture for creating test recipes with Lua states
struct test_recipe_fixture {
  recipe_spec spec;
  std::unique_ptr<recipe> r;

  test_recipe_fixture() {
    spec.identity = "test.package@v1";

    // Create Lua state first
    auto lua_state = std::make_unique<sol::state>();
    lua_state->open_libraries(sol::lib::base, sol::lib::package, sol::lib::coroutine,
                              sol::lib::string, sol::lib::os, sol::lib::math,
                              sol::lib::table, sol::lib::debug, sol::lib::bit32,
                              sol::lib::io);
    lua_envy_install(*lua_state);

    // Initialize options to empty table
    spec.serialized_options = "{}";

    r = std::unique_ptr<recipe>(new recipe{
        .key = recipe_key(spec),
        .spec = &spec,
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

  void set_check_string(std::string_view cmd) {
    lua_pushstring(r->lua->lua_state(), std::string(cmd).c_str());
    lua_setglobal(r->lua->lua_state(), "check");
  }

  void set_check_function(std::string_view lua_code) {
    std::string code = "check = " + std::string(lua_code);
    if (luaL_dostring(r->lua->lua_state(), code.c_str()) != LUA_OK) {
      char const *err = lua_tostring(r->lua->lua_state(), -1);
      throw std::runtime_error(std::string("Failed to set check function: ") +
                               (err ? err : "unknown"));
    }
  }

  void clear_check() {
    lua_pushnil(r->lua->lua_state());
    lua_setglobal(r->lua->lua_state(), "check");
  }
};

}  // namespace

// ============================================================================
// recipe_has_check_verb() tests
// ============================================================================

TEST_CASE("recipe_has_check_verb detects string check") {
  test_recipe_fixture f;
  f.set_check_string("true");

  bool has_check = recipe_has_check_verb(f.r.get(), f.r->lua->lua_state());
  CHECK(has_check);
}

TEST_CASE("recipe_has_check_verb detects function check") {
  test_recipe_fixture f;
  f.set_check_function("function(ctx) return true end");

  bool has_check = recipe_has_check_verb(f.r.get(), f.r->lua->lua_state());
  CHECK(has_check);
}

TEST_CASE("recipe_has_check_verb returns false when no check verb") {
  test_recipe_fixture f;
  // No check verb set

  bool has_check = recipe_has_check_verb(f.r.get(), f.r->lua->lua_state());
  CHECK_FALSE(has_check);
}

TEST_CASE("recipe_has_check_verb returns false for number") {
  test_recipe_fixture f;
  lua_pushnumber(f.r->lua->lua_state(), 42);
  lua_setglobal(f.r->lua->lua_state(), "check");

  bool has_check{ recipe_has_check_verb(f.r.get(), f.r->lua->lua_state()) };
  CHECK_FALSE(has_check);
}

TEST_CASE("recipe_has_check_verb returns false for invalid check type (table)") {
  test_recipe_fixture f;
  lua_newtable(f.r->lua->lua_state());
  lua_setglobal(f.r->lua->lua_state(), "check");

  bool has_check = recipe_has_check_verb(f.r.get(), f.r->lua->lua_state());
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

  lua_State *L{ f.r->lua->lua_state() };
  luaL_dostring(L, "return function(ctx) return true end");
  REQUIRE(lua_isfunction(L, -1));

  sol::protected_function check_func{ sol::stack_object{ L, -1 } };
  lua_pop(L, 1);

  bool result{ run_check_function(f.r.get(), L, check_func) };
  CHECK(result);
}

TEST_CASE("run_check_function returns false when function returns false") {
  test_recipe_fixture f;

  lua_State *L{ f.r->lua->lua_state() };
  luaL_dostring(L, "return function(ctx) return false end");
  REQUIRE(lua_isfunction(L, -1));

  sol::protected_function check_func{ sol::stack_object{ L, -1 } };
  lua_pop(L, 1);

  bool result{ run_check_function(f.r.get(), L, check_func) };
  CHECK_FALSE(result);
}

TEST_CASE("run_check_function returns false when function returns nil") {
  test_recipe_fixture f;

  lua_State *L{ f.r->lua->lua_state() };
  luaL_dostring(L, "return function(ctx) return nil end");
  REQUIRE(lua_isfunction(L, -1));

  sol::protected_function check_func{ sol::stack_object{ L, -1 } };
  lua_pop(L, 1);

  bool result{ run_check_function(f.r.get(), L, check_func) };
  CHECK_FALSE(result);
}

TEST_CASE("run_check_function returns true when function returns truthy value") {
  test_recipe_fixture f;

  lua_State *L{ f.r->lua->lua_state() };
  luaL_dostring(L, "return function(ctx) return 42 end");
  REQUIRE(lua_isfunction(L, -1));

  sol::protected_function check_func{ sol::stack_object{ L, -1 } };
  lua_pop(L, 1);

  bool result{ run_check_function(f.r.get(), L, check_func) };
  CHECK(result);
}

TEST_CASE("run_check_function returns true when function returns string") {
  test_recipe_fixture f;

  lua_State *L{ f.r->lua->lua_state() };
  luaL_dostring(L, "return function(ctx) return 'yes' end");
  REQUIRE(lua_isfunction(L, -1));

  sol::protected_function check_func{ sol::stack_object{ L, -1 } };
  lua_pop(L, 1);

  bool result{ run_check_function(f.r.get(), L, check_func) };
  CHECK(result);
}

TEST_CASE("run_check_function throws when function has Lua error") {
  test_recipe_fixture f;

  lua_State *L{ f.r->lua->lua_state() };
  luaL_dostring(L, "return function(ctx) error('test error') end");
  REQUIRE(lua_isfunction(L, -1));

  sol::protected_function check_func{ sol::stack_object{ L, -1 } };
  lua_pop(L, 1);

  CHECK_THROWS_AS(run_check_function(f.r.get(), L, check_func), std::runtime_error);
}

TEST_CASE("run_check_function receives empty ctx table") {
  test_recipe_fixture f;

  lua_State *L{ f.r->lua->lua_state() };
  luaL_dostring(L, R"(
    return function(ctx)
      return type(ctx) == 'table'
    end
  )");
  REQUIRE(lua_isfunction(L, -1));

  sol::protected_function check_func{ sol::stack_object{ L, -1 } };
  lua_pop(L, 1);

  bool result{ run_check_function(f.r.get(), L, check_func) };
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

  bool result = run_check_verb(f.r.get(), eng, f.r->lua->lua_state());
  CHECK(result);
}

TEST_CASE("run_check_verb dispatches to function handler") {
  test_recipe_fixture f;
  f.set_check_function("function(ctx) return true end");

  cache test_cache;
  engine eng{ test_cache, std::nullopt };

  bool result = run_check_verb(f.r.get(), eng, f.r->lua->lua_state());
  CHECK(result);
}

TEST_CASE("run_check_verb returns false when no check verb") {
  test_recipe_fixture f;
  // No check verb set

  cache test_cache;
  engine eng{ test_cache, std::nullopt };

  bool result = run_check_verb(f.r.get(), eng, f.r->lua->lua_state());
  CHECK_FALSE(result);
}

TEST_CASE("run_check_verb returns false for table check type") {
  test_recipe_fixture f;
  lua_newtable(f.r->lua->lua_state());
  lua_setglobal(f.r->lua->lua_state(), "check");

  cache test_cache;
  engine eng{ test_cache, std::nullopt };

  // Tables are not functions or strings, so check verb is not present
  bool result = run_check_verb(f.r.get(), eng, f.r->lua->lua_state());
  CHECK_FALSE(result);
}

TEST_CASE("run_check_verb string check respects exit code") {
  test_recipe_fixture f;

  cache test_cache;
  engine eng{ test_cache, std::nullopt };

  SUBCASE("exit 0 returns true") {
    f.set_check_string("exit 0");
    bool result = run_check_verb(f.r.get(), eng, f.r->lua->lua_state());
    CHECK(result);
  }

  SUBCASE("exit 1 returns false") {
    f.set_check_string("exit 1");
    bool result = run_check_verb(f.r.get(), eng, f.r->lua->lua_state());
    CHECK_FALSE(result);
  }
}

TEST_CASE("run_check_verb function check respects return value") {
  test_recipe_fixture f;

  cache test_cache;
  engine eng{ test_cache, std::nullopt };

  SUBCASE("function returns true") {
    f.set_check_function("function(ctx) return true end");
    bool result = run_check_verb(f.r.get(), eng, f.r->lua->lua_state());
    CHECK(result);
  }

  SUBCASE("function returns false") {
    f.set_check_function("function(ctx) return false end");
    bool result = run_check_verb(f.r.get(), eng, f.r->lua->lua_state());
    CHECK_FALSE(result);
  }
}

// ============================================================================
// Error handling tests
// ============================================================================

TEST_CASE("run_check_function propagates Lua error with context") {
  test_recipe_fixture f;
  f.spec.identity = "my.package@v1";

  lua_State *L{ f.r->lua->lua_state() };
  luaL_dostring(L, "return function(ctx) error('something went wrong') end");
  REQUIRE(lua_isfunction(L, -1));

  sol::protected_function check_func{ sol::stack_object{ L, -1 } };
  lua_pop(L, 1);

  try {
    run_check_function(f.r.get(), L, check_func);
    FAIL("Expected exception");
  } catch (std::runtime_error const &e) {
    std::string msg{ e.what() };
    CHECK(msg.find("my.package@v1") != std::string::npos);
    CHECK(msg.find("something went wrong") != std::string::npos);
  }
}

}  // namespace envy
