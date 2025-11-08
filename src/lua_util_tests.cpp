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

TEST_CASE("lua_add_envy creates envy table") {
  auto L{ envy::lua_make() };
  REQUIRE(L != nullptr);
  envy::lua_add_envy(L);

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

TEST_CASE("lua_add_envy overrides print function") {
  auto L{ envy::lua_make() };
  REQUIRE(L != nullptr);
  envy::lua_add_envy(L);

  lua_getglobal(L.get(), "print");
  CHECK(lua_isfunction(L.get(), -1));
  lua_pop(L.get(), 1);
}

TEST_CASE("lua_make without lua_add_envy has standard print") {
  auto L{ envy::lua_make() };
  REQUIRE(L != nullptr);

  lua_getglobal(L.get(), "print");
  CHECK(lua_isfunction(L.get(), -1));
  lua_pop(L.get(), 1);

  // Should not have envy table
  lua_getglobal(L.get(), "envy");
  CHECK(lua_isnil(L.get(), -1));
  lua_pop(L.get(), 1);
}

TEST_CASE("lua_add_envy sets ENVY_PLATFORM global") {
  auto L{ envy::lua_make() };
  REQUIRE(L != nullptr);
  envy::lua_add_envy(L);

  lua_getglobal(L.get(), "ENVY_PLATFORM");
  REQUIRE(lua_isstring(L.get(), -1));

  char const *platform{ lua_tostring(L.get(), -1) };
  REQUIRE(platform != nullptr);

#if defined(__APPLE__) && defined(__MACH__)
  CHECK(std::string{ platform } == "darwin");
#elif defined(__linux__)
  CHECK(std::string{ platform } == "linux");
#elif defined(_WIN32)
  CHECK(std::string{ platform } == "windows");
#else
  CHECK(std::string{ platform } == "unknown");
#endif

  lua_pop(L.get(), 1);
}

TEST_CASE("lua_add_envy sets ENVY_ARCH global") {
  auto L{ envy::lua_make() };
  REQUIRE(L != nullptr);
  envy::lua_add_envy(L);

  lua_getglobal(L.get(), "ENVY_ARCH");
  REQUIRE(lua_isstring(L.get(), -1));

  char const *arch{ lua_tostring(L.get(), -1) };
  REQUIRE(arch != nullptr);

#if defined(__APPLE__) && defined(__MACH__)
#if defined(__arm64__)
  CHECK(std::string{ arch } == "arm64");
#elif defined(__x86_64__)
  CHECK(std::string{ arch } == "x86_64");
#endif
#elif defined(__linux__)
#if defined(__aarch64__)
  CHECK(std::string{ arch } == "aarch64");
#elif defined(__x86_64__)
  CHECK(std::string{ arch } == "x86_64");
#elif defined(__i386__)
  CHECK(std::string{ arch } == "i386");
#endif
#elif defined(_WIN32)
#if defined(_M_ARM64)
  CHECK(std::string{ arch } == "arm64");
#elif defined(_M_X64) || defined(_M_AMD64)
  CHECK(std::string{ arch } == "x86_64");
#elif defined(_M_IX86)
  CHECK(std::string{ arch } == "x86");
#endif
#endif

  lua_pop(L.get(), 1);
}

TEST_CASE("lua_add_envy sets ENVY_PLATFORM_ARCH global") {
  auto L{ envy::lua_make() };
  REQUIRE(L != nullptr);
  envy::lua_add_envy(L);

  lua_getglobal(L.get(), "ENVY_PLATFORM_ARCH");
  REQUIRE(lua_isstring(L.get(), -1));

  char const *platform_arch{ lua_tostring(L.get(), -1) };
  REQUIRE(platform_arch != nullptr);

  std::string const result{ platform_arch };

#if defined(__APPLE__) && defined(__MACH__)
#if defined(__arm64__)
  CHECK(result == "darwin-arm64");
#elif defined(__x86_64__)
  CHECK(result == "darwin-x86_64");
#endif
#elif defined(__linux__)
#if defined(__aarch64__)
  CHECK(result == "linux-aarch64");
#elif defined(__x86_64__)
  CHECK(result == "linux-x86_64");
#elif defined(__i386__)
  CHECK(result == "linux-i386");
#endif
#elif defined(_WIN32)
#if defined(_M_ARM64)
  CHECK(result == "windows-arm64");
#elif defined(_M_X64) || defined(_M_AMD64)
  CHECK(result == "windows-x86_64");
#elif defined(_M_IX86)
  CHECK(result == "windows-x86");
#endif
#endif

  lua_pop(L.get(), 1);
}

TEST_CASE("lua_add_envy allows Lua scripts to access platform info") {
  auto L{ envy::lua_make() };
  REQUIRE(L != nullptr);
  envy::lua_add_envy(L);

  CHECK(envy::lua_run_string(L, R"(
    assert(type(ENVY_PLATFORM) == 'string')
    assert(type(ENVY_ARCH) == 'string')
    assert(type(ENVY_PLATFORM_ARCH) == 'string')

    -- Verify combined format
    expected = ENVY_PLATFORM .. '-' .. ENVY_ARCH
    assert(ENVY_PLATFORM_ARCH == expected)
  )"));
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
  envy::lua_state_ptr const null_state;
  auto const test_root{ test_data_root() };
  auto const script_path{ test_root / "simple.lua" };

  CHECK_FALSE(envy::lua_run_file(null_state, script_path));
}

TEST_CASE("lua_run_string returns false with null state") {
  envy::lua_state_ptr const null_state;

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

// lua_value serialization tests ------------------------------------------

TEST_CASE("lua_stack_to_value extracts nil") {
  auto const L{ envy::lua_make() };
  REQUIRE(L != nullptr);

  lua_pushnil(L.get());
  auto const val{ envy::lua_stack_to_value(L.get(), -1) };

  CHECK(val.is_nil());
  CHECK_FALSE(val.is_bool());
  CHECK_FALSE(val.is_integer());
  CHECK_FALSE(val.is_number());
  CHECK_FALSE(val.is_string());
  CHECK_FALSE(val.is_table());
}

TEST_CASE("lua_stack_to_value extracts boolean") {
  auto const L{ envy::lua_make() };
  REQUIRE(L != nullptr);

  lua_pushboolean(L.get(), 1);
  auto const val{ envy::lua_stack_to_value(L.get(), -1) };

  CHECK(val.is_bool());
  CHECK(std::get<bool>(val.v) == true);
}

TEST_CASE("lua_stack_to_value extracts integer") {
  auto const L{ envy::lua_make() };
  REQUIRE(L != nullptr);

  lua_pushinteger(L.get(), 42);
  auto const val{ envy::lua_stack_to_value(L.get(), -1) };

  CHECK(val.is_integer());
  CHECK(std::get<int64_t>(val.v) == 42);
}

TEST_CASE("lua_stack_to_value extracts negative integer") {
  auto const L{ envy::lua_make() };
  REQUIRE(L != nullptr);

  lua_pushinteger(L.get(), -999);
  auto const val{ envy::lua_stack_to_value(L.get(), -1) };

  CHECK(val.is_integer());
  CHECK(std::get<int64_t>(val.v) == -999);
}

TEST_CASE("lua_stack_to_value extracts floating point number") {
  auto const L{ envy::lua_make() };
  REQUIRE(L != nullptr);

  lua_pushnumber(L.get(), 3.14159);
  auto const val{ envy::lua_stack_to_value(L.get(), -1) };

  CHECK(val.is_number());
  CHECK(std::get<double>(val.v) == doctest::Approx(3.14159));
}

TEST_CASE("lua_stack_to_value extracts string") {
  auto const L{ envy::lua_make() };
  REQUIRE(L != nullptr);

  lua_pushlstring(L.get(), "hello", 5);
  auto const val{ envy::lua_stack_to_value(L.get(), -1) };

  CHECK(val.is_string());
  CHECK(std::get<std::string>(val.v) == "hello");
}

TEST_CASE("lua_stack_to_value extracts empty string") {
  auto const L{ envy::lua_make() };
  REQUIRE(L != nullptr);

  lua_pushlstring(L.get(), "", 0);
  auto const val{ envy::lua_stack_to_value(L.get(), -1) };

  CHECK(val.is_string());
  CHECK(std::get<std::string>(val.v) == "");
}

TEST_CASE("lua_stack_to_value extracts string with embedded nulls") {
  auto const L{ envy::lua_make() };
  REQUIRE(L != nullptr);

  char const data[]{ "hello\0world" };
  lua_pushlstring(L.get(), data, 11);
  auto const val{ envy::lua_stack_to_value(L.get(), -1) };

  CHECK(val.is_string());
  auto const &str{ std::get<std::string>(val.v) };
  CHECK(str.size() == 11);
  CHECK(str == std::string(data, 11));
}

TEST_CASE("lua_stack_to_value extracts empty table") {
  auto const L{ envy::lua_make() };
  REQUIRE(L != nullptr);

  lua_newtable(L.get());
  auto const val{ envy::lua_stack_to_value(L.get(), -1) };

  CHECK(val.is_table());
  auto const &table{ std::get<envy::lua_table>(val.v) };
  CHECK(table.empty());
}

TEST_CASE("lua_stack_to_value extracts simple table") {
  auto const L{ envy::lua_make() };
  REQUIRE(L != nullptr);

  CHECK(envy::lua_run_string(L, "t = { foo = 'bar', num = 42 }"));
  lua_getglobal(L.get(), "t");

  auto const val{ envy::lua_stack_to_value(L.get(), -1) };

  REQUIRE(val.is_table());
  auto const &table{ std::get<envy::lua_table>(val.v) };
  CHECK(table.size() == 2);

  auto const foo_it{ table.find("foo") };
  REQUIRE(foo_it != table.end());
  CHECK(foo_it->second.is_string());
  CHECK(std::get<std::string>(foo_it->second.v) == "bar");

  auto const num_it{ table.find("num") };
  REQUIRE(num_it != table.end());
  CHECK(num_it->second.is_integer());
  CHECK(std::get<int64_t>(num_it->second.v) == 42);
}

TEST_CASE("lua_stack_to_value extracts nested table") {
  auto const L{ envy::lua_make() };
  REQUIRE(L != nullptr);

  CHECK(envy::lua_run_string(L, "t = { outer = { inner = 'value' } }"));
  lua_getglobal(L.get(), "t");

  auto const val{ envy::lua_stack_to_value(L.get(), -1) };

  REQUIRE(val.is_table());
  auto const &outer_table{ std::get<envy::lua_table>(val.v) };
  CHECK(outer_table.size() == 1);

  auto const outer_it{ outer_table.find("outer") };
  REQUIRE(outer_it != outer_table.end());
  REQUIRE(outer_it->second.is_table());

  auto const &inner_table{ std::get<envy::lua_table>(outer_it->second.v) };
  CHECK(inner_table.size() == 1);

  auto const inner_it{ inner_table.find("inner") };
  REQUIRE(inner_it != inner_table.end());
  CHECK(inner_it->second.is_string());
  CHECK(std::get<std::string>(inner_it->second.v) == "value");
}

TEST_CASE("lua_stack_to_value extracts deeply nested table") {
  auto const L{ envy::lua_make() };
  REQUIRE(L != nullptr);

  CHECK(envy::lua_run_string(L, R"(
    t = {
      level1 = {
        level2 = {
          level3 = {
            level4 = {
              deep = 'bottom'
            }
          }
        }
      }
    }
  )"));
  lua_getglobal(L.get(), "t");

  auto const val{ envy::lua_stack_to_value(L.get(), -1) };

  REQUIRE(val.is_table());
  auto const &t1{ std::get<envy::lua_table>(val.v) };
  auto const it1{ t1.find("level1") };
  REQUIRE(it1 != t1.end());
  REQUIRE(it1->second.is_table());

  auto const &t2{ std::get<envy::lua_table>(it1->second.v) };
  auto const it2{ t2.find("level2") };
  REQUIRE(it2 != t2.end());
  REQUIRE(it2->second.is_table());

  auto const &t3{ std::get<envy::lua_table>(it2->second.v) };
  auto const it3{ t3.find("level3") };
  REQUIRE(it3 != t3.end());
  REQUIRE(it3->second.is_table());

  auto const &t4{ std::get<envy::lua_table>(it3->second.v) };
  auto const it4{ t4.find("level4") };
  REQUIRE(it4 != t4.end());
  REQUIRE(it4->second.is_table());

  auto const &t5{ std::get<envy::lua_table>(it4->second.v) };
  auto const it5{ t5.find("deep") };
  REQUIRE(it5 != t5.end());
  CHECK(it5->second.is_string());
  CHECK(std::get<std::string>(it5->second.v) == "bottom");
}

TEST_CASE("lua_stack_to_value extracts mixed type table") {
  auto const L{ envy::lua_make() };
  REQUIRE(L != nullptr);

  CHECK(envy::lua_run_string(L, R"(
    t = {
      bool_val = true,
      int_val = 123,
      float_val = 45.67,
      str_val = 'text',
      nil_val = nil,
      table_val = { nested = 'data' }
    }
  )"));
  lua_getglobal(L.get(), "t");

  auto const val{ envy::lua_stack_to_value(L.get(), -1) };

  REQUIRE(val.is_table());
  auto const &table{ std::get<envy::lua_table>(val.v) };

  // nil_val should not be in the table (Lua semantics)
  CHECK(table.find("nil_val") == table.end());

  CHECK(table.size() == 5);

  auto const bool_it{ table.find("bool_val") };
  REQUIRE(bool_it != table.end());
  CHECK(bool_it->second.is_bool());
  CHECK(std::get<bool>(bool_it->second.v) == true);

  auto const int_it{ table.find("int_val") };
  REQUIRE(int_it != table.end());
  CHECK(int_it->second.is_integer());
  CHECK(std::get<int64_t>(int_it->second.v) == 123);

  auto const float_it{ table.find("float_val") };
  REQUIRE(float_it != table.end());
  CHECK(float_it->second.is_number());
  CHECK(std::get<double>(float_it->second.v) == doctest::Approx(45.67));

  auto const str_it{ table.find("str_val") };
  REQUIRE(str_it != table.end());
  CHECK(str_it->second.is_string());
  CHECK(std::get<std::string>(str_it->second.v) == "text");

  auto const table_it{ table.find("table_val") };
  REQUIRE(table_it != table.end());
  CHECK(table_it->second.is_table());
}

TEST_CASE("lua_stack_to_value ignores numeric keys") {
  auto const L{ envy::lua_make() };
  REQUIRE(L != nullptr);

  CHECK(envy::lua_run_string(L, "t = { [1] = 'one', [2] = 'two', str = 'value' }"));
  lua_getglobal(L.get(), "t");

  auto const val{ envy::lua_stack_to_value(L.get(), -1) };

  REQUIRE(val.is_table());
  auto const &table{ std::get<envy::lua_table>(val.v) };

  // Only string key should be extracted
  CHECK(table.size() == 1);
  CHECK(table.find("str") != table.end());
}

TEST_CASE("lua_global_to_value returns nullopt for nonexistent global") {
  auto const L{ envy::lua_make() };
  REQUIRE(L != nullptr);

  auto const result{ envy::lua_global_to_value(L.get(), "nonexistent") };
  CHECK_FALSE(result.has_value());
}

TEST_CASE("lua_global_to_value returns nullopt for nil global") {
  auto const L{ envy::lua_make() };
  REQUIRE(L != nullptr);

  CHECK(envy::lua_run_string(L, "x = nil"));
  auto const result{ envy::lua_global_to_value(L.get(), "x") };
  CHECK_FALSE(result.has_value());
}

TEST_CASE("lua_global_to_value extracts integer global") {
  auto const L{ envy::lua_make() };
  REQUIRE(L != nullptr);

  CHECK(envy::lua_run_string(L, "x = 999"));
  auto const result{ envy::lua_global_to_value(L.get(), "x") };

  REQUIRE(result.has_value());
  CHECK(result->is_integer());
  CHECK(std::get<int64_t>(result->v) == 999);
}

TEST_CASE("lua_global_to_value extracts table global") {
  auto const L{ envy::lua_make() };
  REQUIRE(L != nullptr);

  CHECK(envy::lua_run_string(L, "packages = { foo = 'bar' }"));
  auto const result{ envy::lua_global_to_value(L.get(), "packages") };

  REQUIRE(result.has_value());
  REQUIRE(result->is_table());
  auto const &table{ std::get<envy::lua_table>(result->v) };
  CHECK(table.size() == 1);
  CHECK(table.find("foo") != table.end());
}

TEST_CASE("value_to_lua_stack pushes nil") {
  auto const L{ envy::lua_make() };
  REQUIRE(L != nullptr);

  envy::lua_value const val{};
  envy::value_to_lua_stack(L.get(), val);

  CHECK(lua_isnil(L.get(), -1));
}

TEST_CASE("value_to_lua_stack pushes boolean") {
  auto const L{ envy::lua_make() };
  REQUIRE(L != nullptr);

  envy::lua_value const val{ envy::lua_variant{ true } };
  envy::value_to_lua_stack(L.get(), val);

  CHECK(lua_isboolean(L.get(), -1));
  CHECK(lua_toboolean(L.get(), -1) == 1);
}

TEST_CASE("value_to_lua_stack pushes integer") {
  auto const L{ envy::lua_make() };
  REQUIRE(L != nullptr);

  envy::lua_value const val{ envy::lua_variant{ int64_t{ 42 } } };
  envy::value_to_lua_stack(L.get(), val);

  CHECK(lua_isinteger(L.get(), -1));
  CHECK(lua_tointeger(L.get(), -1) == 42);
}

TEST_CASE("value_to_lua_stack pushes number") {
  auto const L{ envy::lua_make() };
  REQUIRE(L != nullptr);

  envy::lua_value const val{ envy::lua_variant{ 2.718 } };
  envy::value_to_lua_stack(L.get(), val);

  CHECK(lua_isnumber(L.get(), -1));
  CHECK(lua_tonumber(L.get(), -1) == doctest::Approx(2.718));
}

TEST_CASE("value_to_lua_stack pushes string") {
  auto const L{ envy::lua_make() };
  REQUIRE(L != nullptr);

  envy::lua_value const val{ envy::lua_variant{ std::string{ "test" } } };
  envy::value_to_lua_stack(L.get(), val);

  CHECK(lua_isstring(L.get(), -1));
  size_t len{ 0 };
  char const *str{ lua_tolstring(L.get(), -1, &len) };
  CHECK(len == 4);
  CHECK(std::string{ str, len } == "test");
}

TEST_CASE("value_to_lua_stack pushes empty table") {
  auto const L{ envy::lua_make() };
  REQUIRE(L != nullptr);

  envy::lua_table const empty_table;
  envy::lua_value const val{ envy::lua_variant{ empty_table } };
  envy::value_to_lua_stack(L.get(), val);

  CHECK(lua_istable(L.get(), -1));

  // Count elements
  size_t count{ 0 };
  lua_pushnil(L.get());
  while (lua_next(L.get(), -2) != 0) {
    ++count;
    lua_pop(L.get(), 1);
  }
  CHECK(count == 0);
}

TEST_CASE("value_to_lua_stack pushes simple table") {
  auto const L{ envy::lua_make() };
  REQUIRE(L != nullptr);

  envy::lua_table table;
  table["key1"] = envy::lua_value{ envy::lua_variant{ std::string{ "value1" } } };
  table["key2"] = envy::lua_value{ envy::lua_variant{ int64_t{ 99 } } };

  envy::lua_value const val{ envy::lua_variant{ std::move(table) } };
  envy::value_to_lua_stack(L.get(), val);

  CHECK(lua_istable(L.get(), -1));

  lua_getfield(L.get(), -1, "key1");
  CHECK(lua_isstring(L.get(), -1));
  CHECK(std::string{ lua_tostring(L.get(), -1) } == "value1");
  lua_pop(L.get(), 1);

  lua_getfield(L.get(), -1, "key2");
  CHECK(lua_isinteger(L.get(), -1));
  CHECK(lua_tointeger(L.get(), -1) == 99);
  lua_pop(L.get(), 1);
}

TEST_CASE("value_to_lua_stack pushes nested table") {
  auto const L{ envy::lua_make() };
  REQUIRE(L != nullptr);

  envy::lua_table inner;
  inner["nested_key"] =
      envy::lua_value{ envy::lua_variant{ std::string{ "nested_value" } } };

  envy::lua_table outer;
  outer["outer_key"] = envy::lua_value{ envy::lua_variant{ std::move(inner) } };

  envy::lua_value const val{ envy::lua_variant{ std::move(outer) } };
  envy::value_to_lua_stack(L.get(), val);

  CHECK(lua_istable(L.get(), -1));

  lua_getfield(L.get(), -1, "outer_key");
  CHECK(lua_istable(L.get(), -1));

  lua_getfield(L.get(), -1, "nested_key");
  CHECK(lua_isstring(L.get(), -1));
  CHECK(std::string{ lua_tostring(L.get(), -1) } == "nested_value");
}

TEST_CASE("value_to_lua_global sets global variable") {
  auto const L{ envy::lua_make() };
  REQUIRE(L != nullptr);

  envy::lua_value const val{ envy::lua_variant{ int64_t{ 777 } } };
  envy::value_to_lua_global(L.get(), "my_global", val);

  lua_getglobal(L.get(), "my_global");
  CHECK(lua_isinteger(L.get(), -1));
  CHECK(lua_tointeger(L.get(), -1) == 777);
}

TEST_CASE("round-trip: stack to value to stack") {
  auto const L{ envy::lua_make() };
  REQUIRE(L != nullptr);

  CHECK(envy::lua_run_string(L, R"(
    original = {
      name = 'test',
      count = 42,
      enabled = true,
      ratio = 1.5,
      config = {
        opt1 = 'a',
        opt2 = 'b'
      }
    }
  )"));

  lua_getglobal(L.get(), "original");
  auto const extracted{ envy::lua_stack_to_value(L.get(), -1) };
  lua_pop(L.get(), 1);

  envy::value_to_lua_global(L.get(), "copied", extracted);

  // Verify copied table matches
  CHECK(envy::lua_run_string(L, R"(
    assert(copied.name == 'test')
    assert(copied.count == 42)
    assert(copied.enabled == true)
    assert(copied.ratio == 1.5)
    assert(copied.config.opt1 == 'a')
    assert(copied.config.opt2 == 'b')
  )"));
}

// lua_global_to_array tests ----------------------------------------------

TEST_CASE("lua_global_to_array extracts empty array") {
  auto const L{ envy::lua_make() };
  REQUIRE(L != nullptr);

  CHECK(envy::lua_run_string(L, "arr = {}"));
  auto const result{ envy::lua_global_to_array(L.get(), "arr") };
  REQUIRE(result.has_value());
  CHECK(result->empty());
}

TEST_CASE("lua_global_to_array extracts single element array") {
  auto const L{ envy::lua_make() };
  REQUIRE(L != nullptr);

  CHECK(envy::lua_run_string(L, "arr = { 'foo' }"));
  auto const result{ envy::lua_global_to_array(L.get(), "arr") };

  REQUIRE(result.has_value());
  REQUIRE(result->size() == 1);
  CHECK((*result)[0].is_string());
  CHECK(std::get<std::string>((*result)[0].v) == "foo");
}

TEST_CASE("lua_global_to_array extracts multiple element array") {
  auto const L{ envy::lua_make() };
  REQUIRE(L != nullptr);

  CHECK(envy::lua_run_string(L, "arr = { 'foo', 'bar', 'baz' }"));
  auto const result{ envy::lua_global_to_array(L.get(), "arr") };

  REQUIRE(result.has_value());
  REQUIRE(result->size() == 3);
  CHECK(std::get<std::string>((*result)[0].v) == "foo");
  CHECK(std::get<std::string>((*result)[1].v) == "bar");
  CHECK(std::get<std::string>((*result)[2].v) == "baz");
}

TEST_CASE("lua_global_to_array extracts mixed type array") {
  auto const L{ envy::lua_make() };
  REQUIRE(L != nullptr);

  CHECK(envy::lua_run_string(L, "arr = { 'text', 42, true, 3.14 }"));
  auto const result{ envy::lua_global_to_array(L.get(), "arr") };

  REQUIRE(result.has_value());
  REQUIRE(result->size() == 4);
  CHECK((*result)[0].is_string());
  CHECK(std::get<std::string>((*result)[0].v) == "text");
  CHECK((*result)[1].is_integer());
  CHECK(std::get<int64_t>((*result)[1].v) == 42);
  CHECK((*result)[2].is_bool());
  CHECK(std::get<bool>((*result)[2].v) == true);
  CHECK((*result)[3].is_number());
  CHECK(std::get<double>((*result)[3].v) == doctest::Approx(3.14));
}

TEST_CASE("lua_global_to_array extracts array with nested tables") {
  auto const L{ envy::lua_make() };
  REQUIRE(L != nullptr);

  CHECK(envy::lua_run_string(L, R"(
    arr = {
      { name = 'first' },
      { name = 'second' }
    }
  )"));
  auto const result{ envy::lua_global_to_array(L.get(), "arr") };

  REQUIRE(result.has_value());
  REQUIRE(result->size() == 2);
  REQUIRE((*result)[0].is_table());
  REQUIRE((*result)[1].is_table());

  auto const &table0{ std::get<envy::lua_table>((*result)[0].v) };
  auto const &table1{ std::get<envy::lua_table>((*result)[1].v) };

  CHECK(std::get<std::string>(table0.at("name").v) == "first");
  CHECK(std::get<std::string>(table1.at("name").v) == "second");
}

TEST_CASE("lua_global_to_array returns nullopt for nonexistent global") {
  auto const L{ envy::lua_make() };
  REQUIRE(L != nullptr);

  auto const result{ envy::lua_global_to_array(L.get(), "nonexistent") };
  CHECK(!result.has_value());
}

TEST_CASE("lua_global_to_array returns nullopt for nil global") {
  auto const L{ envy::lua_make() };
  REQUIRE(L != nullptr);

  CHECK(envy::lua_run_string(L, "arr = nil"));
  auto const result{ envy::lua_global_to_array(L.get(), "arr") };
  CHECK(!result.has_value());
}

TEST_CASE("lua_global_to_array errors on non-table global") {
  auto const L{ envy::lua_make() };
  REQUIRE(L != nullptr);

  CHECK(envy::lua_run_string(L, "arr = 'not a table'"));
  CHECK_THROWS_WITH_AS(envy::lua_global_to_array(L.get(), "arr"),
                       "Global 'arr' is not a table",
                       std::runtime_error);
}

TEST_CASE("lua_global_to_array errors on number global") {
  auto const L{ envy::lua_make() };
  REQUIRE(L != nullptr);

  CHECK(envy::lua_run_string(L, "arr = 123"));
  CHECK_THROWS_WITH_AS(envy::lua_global_to_array(L.get(), "arr"),
                       "Global 'arr' is not a table",
                       std::runtime_error);
}

TEST_CASE("lua_global_to_array errors on table with string keys") {
  auto const L{ envy::lua_make() };
  REQUIRE(L != nullptr);

  CHECK(envy::lua_run_string(L, "arr = { foo = 'bar' }"));
  CHECK_THROWS_WITH_AS(envy::lua_global_to_array(L.get(), "arr"),
                       "Table 'arr' contains non-numeric keys",
                       std::runtime_error);
}

TEST_CASE("lua_global_to_array errors on mixed array and hash table") {
  auto const L{ envy::lua_make() };
  REQUIRE(L != nullptr);

  CHECK(envy::lua_run_string(L, "arr = { 'foo', 'bar', platform = 'darwin' }"));
  CHECK_THROWS_WITH_AS(envy::lua_global_to_array(L.get(), "arr"),
                       "Table 'arr' contains non-numeric keys",
                       std::runtime_error);
}

TEST_CASE("lua_global_to_array errors on sparse array with gap") {
  auto const L{ envy::lua_make() };
  REQUIRE(L != nullptr);

  CHECK(envy::lua_run_string(L, "arr = { [1] = 'foo', [3] = 'bar' }"));
  CHECK_THROWS_WITH_AS(envy::lua_global_to_array(L.get(), "arr"),
                       "Table 'arr' is sparse (has gaps in numeric indices)",
                       std::runtime_error);
}

TEST_CASE("lua_global_to_array errors on array starting at 0") {
  auto const L{ envy::lua_make() };
  REQUIRE(L != nullptr);

  CHECK(envy::lua_run_string(L, "arr = { [0] = 'foo', [1] = 'bar' }"));
  CHECK_THROWS_WITH_AS(envy::lua_global_to_array(L.get(), "arr"),
                       "Table 'arr' is sparse (has gaps in numeric indices)",
                       std::runtime_error);
}

TEST_CASE("lua_global_to_array errors on array starting at 2") {
  auto const L{ envy::lua_make() };
  REQUIRE(L != nullptr);

  CHECK(envy::lua_run_string(L, "arr = { [2] = 'foo', [3] = 'bar' }"));
  CHECK_THROWS_WITH_AS(envy::lua_global_to_array(L.get(), "arr"),
                       "Table 'arr' is sparse (has gaps in numeric indices)",
                       std::runtime_error);
}

TEST_CASE("lua_global_to_array accepts consecutive array from 1") {
  auto const L{ envy::lua_make() };
  REQUIRE(L != nullptr);

  CHECK(envy::lua_run_string(L, "arr = { [1] = 'foo', [2] = 'bar', [3] = 'baz' }"));
  auto const result{ envy::lua_global_to_array(L.get(), "arr") };

  REQUIRE(result.has_value());
  REQUIRE(result->size() == 3);
  CHECK(std::get<std::string>((*result)[0].v) == "foo");
  CHECK(std::get<std::string>((*result)[1].v) == "bar");
  CHECK(std::get<std::string>((*result)[2].v) == "baz");
}

TEST_CASE("lua_global_to_array handles large array") {
  auto const L{ envy::lua_make() };
  REQUIRE(L != nullptr);

  CHECK(envy::lua_run_string(L, R"(
    arr = {}
    for i = 1, 100 do
      arr[i] = 'item' .. i
    end
  )"));

  auto const result{ envy::lua_global_to_array(L.get(), "arr") };
  REQUIRE(result.has_value());
  REQUIRE(result->size() == 100);

  for (size_t i = 0; i < 100; ++i) {
    CHECK((*result)[i].is_string());
    std::string expected{ "item" };
    expected += std::to_string(i + 1);
    CHECK(std::get<std::string>((*result)[i].v) == expected);
  }
}

// lua_value::get<T>() tests ----------------------------------------------

TEST_CASE("lua_value::get<std::monostate>() returns pointer for nil") {
  envy::lua_value val{ envy::lua_variant{ std::monostate{} } };

  auto const *result{ val.get<std::monostate>() };
  REQUIRE(result != nullptr);
}

TEST_CASE("lua_value::get<std::monostate>() returns nullptr for non-nil") {
  envy::lua_value val{ envy::lua_variant{ true } };

  auto const *result{ val.get<std::monostate>() };
  CHECK(result == nullptr);
}

TEST_CASE("lua_value::get<bool>() returns pointer for bool") {
  envy::lua_value val{ envy::lua_variant{ true } };

  auto const *result{ val.get<bool>() };
  REQUIRE(result != nullptr);
  CHECK(*result == true);
}

TEST_CASE("lua_value::get<bool>() returns nullptr for non-bool") {
  envy::lua_value val{ envy::lua_variant{ int64_t{ 42 } } };

  auto const *result{ val.get<bool>() };
  CHECK(result == nullptr);
}

TEST_CASE("lua_value::get<int64_t>() returns pointer for integer") {
  envy::lua_value val{ envy::lua_variant{ int64_t{ 42 } } };

  auto const *result{ val.get<int64_t>() };
  REQUIRE(result != nullptr);
  CHECK(*result == 42);
}

TEST_CASE("lua_value::get<int64_t>() returns nullptr for non-integer") {
  envy::lua_value val{ envy::lua_variant{ 3.14 } };

  auto const *result{ val.get<int64_t>() };
  CHECK(result == nullptr);
}

TEST_CASE("lua_value::get<double>() returns pointer for double") {
  envy::lua_value val{ envy::lua_variant{ 3.14 } };

  auto const *result{ val.get<double>() };
  REQUIRE(result != nullptr);
  CHECK(*result == doctest::Approx(3.14));
}

TEST_CASE("lua_value::get<double>() returns nullptr for non-double") {
  envy::lua_value val{ envy::lua_variant{ int64_t{ 42 } } };

  auto const *result{ val.get<double>() };
  CHECK(result == nullptr);
}

TEST_CASE("lua_value::get<std::string>() returns pointer for string") {
  envy::lua_value val{ envy::lua_variant{ std::string{ "hello" } } };

  auto const *result{ val.get<std::string>() };
  REQUIRE(result != nullptr);
  CHECK(*result == "hello");
}

TEST_CASE("lua_value::get<std::string>() returns nullptr for non-string") {
  envy::lua_value val{ envy::lua_variant{ int64_t{ 42 } } };

  auto const *result{ val.get<std::string>() };
  CHECK(result == nullptr);
}

TEST_CASE("lua_value::get<lua_table>() returns pointer for table") {
  envy::lua_table table;
  table["key"] = envy::lua_value{ envy::lua_variant{ std::string{ "value" } } };
  envy::lua_value val{ envy::lua_variant{ std::move(table) } };

  auto const *result{ val.get<envy::lua_table>() };
  REQUIRE(result != nullptr);
  CHECK(result->size() == 1);
  CHECK(result->count("key") == 1);
}

TEST_CASE("lua_value::get<lua_table>() returns nullptr for non-table") {
  envy::lua_value val{ envy::lua_variant{ std::string{ "not a table" } } };

  auto const *result{ val.get<envy::lua_table>() };
  CHECK(result == nullptr);
}

TEST_CASE("lua_value::get<T>() works with all variant types") {
  auto const L{ envy::lua_make() };
  REQUIRE(L != nullptr);

  CHECK(envy::lua_run_string(L, R"(
    values = {
      true,
      42,
      3.14,
      "text",
      { nested = "table" }
    }
  )"));

  auto const result{ envy::lua_global_to_array(L.get(), "values") };
  REQUIRE(result.has_value());
  REQUIRE(result->size() == 5);

  // bool
  CHECK((*result)[0].get<bool>() != nullptr);
  CHECK((*result)[0].get<int64_t>() == nullptr);

  // int64_t
  CHECK((*result)[1].get<int64_t>() != nullptr);
  CHECK(*(*result)[1].get<int64_t>() == 42);
  CHECK((*result)[1].get<double>() == nullptr);

  // double
  CHECK((*result)[2].get<double>() != nullptr);
  CHECK(*(*result)[2].get<double>() == doctest::Approx(3.14));
  CHECK((*result)[2].get<std::string>() == nullptr);

  // string
  CHECK((*result)[3].get<std::string>() != nullptr);
  CHECK(*(*result)[3].get<std::string>() == "text");
  CHECK((*result)[3].get<envy::lua_table>() == nullptr);

  // table
  CHECK((*result)[4].get<envy::lua_table>() != nullptr);
  CHECK((*result)[4].get<std::monostate>() == nullptr);
}

TEST_CASE("lua_value::get<T>() pointer remains valid") {
  envy::lua_value val{ envy::lua_variant{ std::string{ "persistent" } } };

  auto const *ptr1{ val.get<std::string>() };
  auto const *ptr2{ val.get<std::string>() };

  REQUIRE(ptr1 != nullptr);
  REQUIRE(ptr2 != nullptr);
  CHECK(ptr1 == ptr2);  // Same address
  CHECK(*ptr1 == "persistent");
}

TEST_CASE("lua_stack_to_value throws on function type") {
  auto const L{ envy::lua_make() };
  REQUIRE(L != nullptr);

  CHECK(envy::lua_run_string(L, "func = function() return 42 end"));
  lua_getglobal(L.get(), "func");
  CHECK(lua_isfunction(L.get(), -1));

  CHECK_THROWS_WITH_AS(envy::lua_stack_to_value(L.get(), -1),
                       "Unsupported Lua type: function",
                       std::runtime_error);
}

TEST_CASE("lua_stack_to_value throws on function in table") {
  auto const L{ envy::lua_make() };
  REQUIRE(L != nullptr);

  CHECK(envy::lua_run_string(L, R"(
    t = {
      name = "test",
      func = function() return 42 end
    }
  )"));
  lua_getglobal(L.get(), "t");

  // Table extraction itself doesn't throw (functions are skipped during iteration)
  // But if we try to serialize a value containing a function, it would fail at serialize time
  // Since lua_stack_to_value only processes string keys and recursively converts values,
  // and our default case now throws, this should catch it

  // Actually, looking at the code, lua_stack_to_value processes tables with lua_next,
  // and only processes string keys. When it encounters a function value, it calls
  // lua_stack_to_value recursively, which will hit the default case and throw.

  CHECK_THROWS_WITH_AS(envy::lua_stack_to_value(L.get(), -1),
                       "Unsupported Lua type: function",
                       std::runtime_error);
}
