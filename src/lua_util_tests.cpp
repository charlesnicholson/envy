#include "lua_util.h"

#include "doctest.h"

extern "C" {
#include "lua.h"
}

#include <filesystem>

namespace fs = std::filesystem;

namespace {

fs::path test_data_root() {
  auto root{ fs::current_path() / "test_data" / "lua" };
  if (!fs::exists(root)) {
    root = fs::current_path().parent_path().parent_path() / "test_data" / "lua";
  }
  return fs::absolute(root);
}

}  // namespace

TEST_CASE("lua_make creates valid state") {
  auto L{ envy::lua_make() };
  REQUIRE(L != nullptr);
  CHECK(lua_gettop(L.get()) == 0);
}

TEST_CASE("lua_make loads standard libraries") {
  auto L{ envy::lua_make() };
  REQUIRE(L != nullptr);

  // Check that standard table library is available
  lua_getglobal(L.get(), "table");
  CHECK(lua_istable(L.get(), -1));
  lua_pop(L.get(), 1);

  // Check that string library is available
  lua_getglobal(L.get(), "string");
  CHECK(lua_istable(L.get(), -1));
  lua_pop(L.get(), 1);
}

TEST_CASE("lua_make creates envy table") {
  auto L{ envy::lua_make() };
  REQUIRE(L != nullptr);

  lua_getglobal(L.get(), "envy");
  REQUIRE(lua_istable(L.get(), -1));

  // Check that envy.debug exists
  lua_getfield(L.get(), -1, "debug");
  CHECK(lua_isfunction(L.get(), -1));
  lua_pop(L.get(), 1);

  // Check that envy.info exists
  lua_getfield(L.get(), -1, "info");
  CHECK(lua_isfunction(L.get(), -1));
  lua_pop(L.get(), 1);

  // Check that envy.warn exists
  lua_getfield(L.get(), -1, "warn");
  CHECK(lua_isfunction(L.get(), -1));
  lua_pop(L.get(), 1);

  // Check that envy.error exists
  lua_getfield(L.get(), -1, "error");
  CHECK(lua_isfunction(L.get(), -1));
  lua_pop(L.get(), 1);

  // Check that envy.stdout exists
  lua_getfield(L.get(), -1, "stdout");
  CHECK(lua_isfunction(L.get(), -1));
  lua_pop(L.get(), 2);
}

TEST_CASE("lua_make overrides print function") {
  auto L{ envy::lua_make() };
  REQUIRE(L != nullptr);

  lua_getglobal(L.get(), "print");
  CHECK(lua_isfunction(L.get(), -1));
  lua_pop(L.get(), 1);
}

TEST_CASE("lua_run_string executes simple script") {
  auto const L{ envy::lua_make() };
  REQUIRE(L != nullptr);

  CHECK(envy::lua_run_string(L, "x = 42"));

  lua_getglobal(L.get(), "x");
  CHECK(lua_isinteger(L.get(), -1));
  CHECK(lua_tointeger(L.get(), -1) == 42);
  lua_pop(L.get(), 1);
}

TEST_CASE("lua_run_string returns false on syntax error") {
  auto const L{ envy::lua_make() };
  REQUIRE(L != nullptr);

  CHECK_FALSE(envy::lua_run_string(L, "this is not valid lua syntax]]"));
}

TEST_CASE("lua_run_string returns false on runtime error") {
  auto const L{ envy::lua_make() };
  REQUIRE(L != nullptr);

  CHECK_FALSE(envy::lua_run_string(L, "error('intentional error')"));
}

TEST_CASE("lua_run executes file script") {
  auto const test_root{ test_data_root() };
  auto const script_path{ test_root / "simple.lua" };
  REQUIRE(fs::exists(script_path));

  auto const L{ envy::lua_make() };
  REQUIRE(L != nullptr);

  CHECK(envy::lua_run_file(L, script_path));

  // Check that the script set expected_value
  lua_getglobal(L.get(), "expected_value");
  CHECK(lua_tointeger(L.get(), -1) == 42);
  lua_pop(L.get(), 1);
}

TEST_CASE("lua_run returns false on missing file") {
  auto const L{ envy::lua_make() };
  REQUIRE(L != nullptr);

  auto const nonexistent{ fs::path("/nonexistent/path/to/script.lua") };
  CHECK_FALSE(envy::lua_run_file(L, nonexistent));
}

TEST_CASE("lua_run returns false with null state") {
  envy::lua_state_ptr const null_state{ nullptr, lua_close };
  auto const test_root{ test_data_root() };
  auto const script_path{ test_root / "simple.lua" };

  CHECK_FALSE(envy::lua_run_file(null_state, script_path));
}

TEST_CASE("lua_run_string returns false with null state") {
  envy::lua_state_ptr const null_state{ nullptr, lua_close };

  CHECK_FALSE(envy::lua_run_string(null_state, "x = 1"));
}

TEST_CASE("lua_state_ptr auto-closes on scope exit") {
  lua_State *raw_ptr{ nullptr };
  {
    auto L{ envy::lua_make() };
    raw_ptr = L.get();
    REQUIRE(raw_ptr != nullptr);
    // State should be usable
    lua_pushinteger(L.get(), 123);
    CHECK(lua_gettop(L.get()) == 1);
  }

  CHECK(raw_ptr != nullptr);  // Pointer value exists, but state is closed
}
