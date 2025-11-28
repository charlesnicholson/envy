#include "recipe_spec.h"

#include "sol/sol.hpp"

#include "doctest.h"

extern "C" {
#include "lua.h"
}

#include <filesystem>

namespace {

namespace fs = std::filesystem;

// Helper to parse a Lua string into a sol::object
sol::object lua_eval(char const *script, sol::state &lua) {
  auto result{ lua.safe_script(script) };
  if (!result.valid()) {
    sol::error err{ result };
    throw std::runtime_error("Lua script failed: " + std::string(err.what()));
  }

  sol::object result_obj{ lua["result"] };
  if (!result_obj.valid()) { throw std::runtime_error("No 'result' global found"); }

  return result_obj;
}

}  // namespace

TEST_CASE("recipe::parse rejects string shorthand") {
  sol::state lua;
  auto lua_val{ lua_eval("result = 'arm.gcc@v2'", lua) };

  CHECK_THROWS_WITH_AS(envy::recipe_spec::parse(lua_val, fs::path("/fake")),
                       doctest::Contains("shorthand string syntax requires table"),
                       std::runtime_error);
}

TEST_CASE("recipe::parse rejects table without source") {
  sol::state lua;
  auto lua_val{ lua_eval("result = { recipe = 'gnu.binutils@v3' }", lua) };

  CHECK_THROWS_WITH_AS(envy::recipe_spec::parse(lua_val, fs::path("/fake")),
                       doctest::Contains("must specify 'source' field"),
                       std::runtime_error);
}

TEST_CASE("recipe::parse allows reference-only dependency when enabled") {
  sol::state lua;
  auto lua_val{ lua_eval("result = { recipe = 'python' }", lua) };

  auto const cfg{ envy::recipe_spec::parse(lua_val, fs::path("/fake"), true) };

  CHECK(cfg.identity == "python");
  CHECK(cfg.is_weak_reference());
  CHECK(cfg.weak == nullptr);
}

TEST_CASE("recipe::parse allows weak dependency with fallback when enabled") {
  sol::state lua;
  auto lua_val{ lua_eval(
      "result = { recipe = 'python', weak = { recipe = 'vendor.python@r4', source = "
      "'/fake/python.lua' } }",
      lua) };

  auto const cfg{ envy::recipe_spec::parse(lua_val, fs::path("/fake"), true) };

  CHECK(cfg.identity == "python");
  CHECK(cfg.is_weak_reference());
  REQUIRE(cfg.weak);
  CHECK(cfg.weak->identity == "vendor.python@r4");
  CHECK(std::holds_alternative<envy::recipe_spec::local_source>(cfg.weak->source));
}

TEST_CASE("recipe::parse parses table with remote source") {
  sol::state lua;
  auto lua_val{ lua_eval(
      "result = { recipe = 'arm.gcc@v2', source = 'https://example.com/gcc.lua', sha256 = "
      "'abc123' }", lua) };

  auto const cfg{ envy::recipe_spec::parse(lua_val, fs::path("/fake")) };

  CHECK(cfg.identity == "arm.gcc@v2");

  auto const *remote{ std::get_if<envy::recipe_spec::remote_source>(&cfg.source) };
  REQUIRE(remote != nullptr);
  CHECK(remote->url == "https://example.com/gcc.lua");
  CHECK(remote->sha256 == "abc123");
}

TEST_CASE("recipe::parse parses table with local source") {
  sol::state lua;
  auto lua_val{ lua_eval(
      "result = { recipe = 'local.tool@v1', source = './recipes/tool.lua' }", lua) };

  auto const cfg{ envy::recipe_spec::parse(lua_val, fs::path("/project/envy.lua")) };

  CHECK(cfg.identity == "local.tool@v1");

  auto const *local{ std::get_if<envy::recipe_spec::local_source>(&cfg.source) };
  REQUIRE(local != nullptr);
  CHECK(local->file_path == fs::path("/project/recipes/tool.lua"));
}

TEST_CASE("recipe::parse resolves relative file paths") {
  sol::state lua;
  auto lua_val{ lua_eval(
      "result = { recipe = 'local.tool@v1', source = '../sibling/tool.lua' }", lua) };

  auto const cfg{ envy::recipe_spec::parse(lua_val, fs::path("/project/sub/envy.lua")) };

  auto const *local{ std::get_if<envy::recipe_spec::local_source>(&cfg.source) };
  REQUIRE(local != nullptr);
  CHECK(local->file_path == fs::path("/project/sibling/tool.lua"));
}

TEST_CASE("recipe::parse parses table with options") {
  sol::state lua;
  auto lua_val{ lua_eval(
      "result = { recipe = 'arm.gcc@v2', source = '/fake/r.lua', options = { version = "
      "'13.2.0', target = "
      "'arm-none-eabi' } }", lua) };

  auto const cfg{ envy::recipe_spec::parse(lua_val, fs::path("/fake")) };

  CHECK(cfg.identity == "arm.gcc@v2");

  // Deserialize and check
  auto opts_result{ lua.safe_script("return " + cfg.serialized_options) };
  REQUIRE(opts_result.valid());
  sol::table opts{ opts_result };
  CHECK(sol::object(opts["version"]).as<std::string>() == "13.2.0");
  CHECK(sol::object(opts["target"]).as<std::string>() == "arm-none-eabi");
}

TEST_CASE("recipe::parse parses table with empty options") {
  sol::state lua;
  auto lua_val{ lua_eval(
      "result = { recipe = 'arm.gcc@v2', source = '/fake/r.lua', options = {} }", lua) };

  auto const cfg{ envy::recipe_spec::parse(lua_val, fs::path("/fake")) };

  CHECK(cfg.identity == "arm.gcc@v2");
  CHECK(cfg.serialized_options == "{}");
}

TEST_CASE("recipe::parse parses table with all fields") {
  sol::state lua;
  auto lua_val{ lua_eval(
      "result = { recipe = 'arm.gcc@v2', source = 'https://example.com/gcc.lua', sha256 = "
      "'abc123', "
      "options = { version = '13.2.0' } }", lua) };

  auto const cfg{ envy::recipe_spec::parse(lua_val, fs::path("/fake")) };

  CHECK(cfg.identity == "arm.gcc@v2");

  auto const *remote{ std::get_if<envy::recipe_spec::remote_source>(&cfg.source) };
  REQUIRE(remote != nullptr);
  CHECK(remote->url == "https://example.com/gcc.lua");
  CHECK(remote->sha256 == "abc123");

  // Deserialize and check
  auto opts_result{ lua.safe_script("return " + cfg.serialized_options) };
  REQUIRE(opts_result.valid());
  sol::table opts{ opts_result };
  CHECK(sol::object(opts["version"]).as<std::string>() == "13.2.0");
}

// Error cases ----------------------------------------------------------------

TEST_CASE("recipe::parse errors on invalid identity format") {
  sol::state lua;
  auto lua_val{ lua_eval(
      "result = { recipe = 'invalid-no-at-sign', source = '/fake/r.lua' }", lua) };

  CHECK_THROWS_WITH_AS(envy::recipe_spec::parse(lua_val, fs::path("/fake")),
                       "Invalid recipe identity format: invalid-no-at-sign",
                       std::runtime_error);
}

TEST_CASE("recipe::parse errors on identity missing namespace") {
  sol::state lua;
  auto lua_val{ lua_eval("result = { recipe = 'gcc@v2', source = '/fake/r.lua' }", lua) };

  CHECK_THROWS_WITH_AS(envy::recipe_spec::parse(lua_val, fs::path("/fake")),
                       "Invalid recipe identity format: gcc@v2",
                       std::runtime_error);
}

TEST_CASE("recipe::parse errors on identity missing name") {
  sol::state lua;
  auto lua_val{ lua_eval("result = { recipe = 'arm.@v2', source = '/fake/r.lua' }", lua) };

  CHECK_THROWS_WITH_AS(envy::recipe_spec::parse(lua_val, fs::path("/fake")),
                       "Invalid recipe identity format: arm.@v2",
                       std::runtime_error);
}

TEST_CASE("recipe::parse errors on identity missing version") {
  sol::state lua;
  auto lua_val{ lua_eval("result = { recipe = 'arm.gcc@', source = '/fake/r.lua' }", lua) };

  CHECK_THROWS_WITH_AS(envy::recipe_spec::parse(lua_val, fs::path("/fake")),
                       "Invalid recipe identity format: arm.gcc@",
                       std::runtime_error);
}

TEST_CASE("recipe::parse errors on identity missing @ sign") {
  sol::state lua;
  auto lua_val{ lua_eval("result = { recipe = 'arm.gcc', source = '/fake/r.lua' }", lua) };

  CHECK_THROWS_WITH_AS(envy::recipe_spec::parse(lua_val, fs::path("/fake")),
                       "Invalid recipe identity format: arm.gcc",
                       std::runtime_error);
}

TEST_CASE("recipe::parse errors on identity missing dot") {
  sol::state lua;
  auto lua_val{ lua_eval("result = { recipe = 'armgcc@v2', source = '/fake/r.lua' }", lua) };

  CHECK_THROWS_WITH_AS(envy::recipe_spec::parse(lua_val, fs::path("/fake")),
                       "Invalid recipe identity format: armgcc@v2",
                       std::runtime_error);
}

TEST_CASE("recipe::parse errors on non-string and non-table value") {
  sol::state lua;
  auto lua_val{ lua_eval("result = 123", lua) };

  CHECK_THROWS_WITH_AS(envy::recipe_spec::parse(lua_val, fs::path("/fake")),
                       "Recipe entry must be string or table",
                       std::runtime_error);
}

TEST_CASE("recipe::parse errors on table missing recipe field") {
  sol::state lua;
  auto lua_val{ lua_eval("result = { source = 'https://example.com/foo.lua' }", lua) };

  CHECK_THROWS_WITH_AS(envy::recipe_spec::parse(lua_val, fs::path("/fake")),
                       "Recipe table missing required 'recipe' field",
                       std::runtime_error);
}

TEST_CASE("recipe::parse errors on non-string recipe field") {
  sol::state lua;
  auto lua_val{ lua_eval("result = { recipe = 123 }", lua) };

  CHECK_THROWS_WITH_AS(envy::recipe_spec::parse(lua_val, fs::path("/fake")),
                       "Recipe 'recipe' field must be string",
                       std::runtime_error);
}

// Test removed - can no longer specify both url and file since we unified to 'source'

TEST_CASE("recipe::parse allows url without sha256 (permissive mode)") {
  sol::state lua;
  auto lua_val{ lua_eval(
      "result = { recipe = 'arm.gcc@v2', source = 'https://example.com/gcc.lua' }", lua) };

  auto const spec{ envy::recipe_spec::parse(lua_val, fs::path("/fake")) };
  CHECK(spec.identity == "arm.gcc@v2");
  CHECK(spec.is_remote());
  auto const *remote{ std::get_if<envy::recipe_spec::remote_source>(&spec.source) };
  REQUIRE(remote != nullptr);
  CHECK(remote->url == "https://example.com/gcc.lua");
  CHECK(remote->sha256.empty());  // No SHA256 provided (permissive)
}

TEST_CASE("recipe::parse errors on non-string source") {
  sol::state lua;
  auto lua_val{ lua_eval(
      "result = { recipe = 'arm.gcc@v2', source = 123, sha256 = 'abc' }", lua) };

  CHECK_THROWS_WITH_AS(envy::recipe_spec::parse(lua_val, fs::path("/fake")),
                       "Recipe 'source' field must be string or table",
                       std::runtime_error);
}

TEST_CASE("recipe::parse errors on non-string sha256") {
  sol::state lua;
  auto lua_val{ lua_eval(
      "result = { recipe = 'arm.gcc@v2', source = 'https://example.com/gcc.lua', sha256 = "
      "123 "
      "}", lua) };

  CHECK_THROWS_WITH_AS(envy::recipe_spec::parse(lua_val, fs::path("/fake")),
                       "Recipe 'sha256' field must be string",
                       std::runtime_error);
}

TEST_CASE("recipe::parse errors on non-string source (local)") {
  sol::state lua;
  auto lua_val{ lua_eval("result = { recipe = 'local.tool@v1', source = 123 }", lua) };

  CHECK_THROWS_WITH_AS(envy::recipe_spec::parse(lua_val, fs::path("/fake")),
                       "Recipe 'source' field must be string or table",
                       std::runtime_error);
}

TEST_CASE("recipe::parse errors on non-table options") {
  sol::state lua;
  auto lua_val{ lua_eval(
      "result = { recipe = 'arm.gcc@v2', source = '/fake/r.lua', options = 'not a table' "
      "}", lua) };

  CHECK_THROWS_WITH_AS(envy::recipe_spec::parse(lua_val, fs::path("/fake")),
                       "Recipe 'options' field must be table",
                       std::runtime_error);
}

TEST_CASE("recipe::parse accepts non-string option values") {
  sol::state lua;
  auto lua_val{ lua_eval(
      "result = { recipe = 'arm.gcc@v2', source = '/fake/r.lua', options = { version = 123, "
      "debug = true, nested = { key = 'value' } } }", lua) };

  auto const cfg{ envy::recipe_spec::parse(lua_val, fs::path("/fake")) };

  // Deserialize and check
  auto opts_result{ lua.safe_script("return " + cfg.serialized_options) };
  REQUIRE(opts_result.valid());
  sol::table opts{ opts_result };
  CHECK(sol::object(opts["version"]).is<lua_Integer>());
  CHECK(sol::object(opts["version"]).as<int64_t>() == 123);
  CHECK(sol::object(opts["debug"]).is<bool>());
  CHECK(sol::object(opts["debug"]).as<bool>() == true);
  CHECK(sol::object(opts["nested"]).is<sol::table>());
}

TEST_CASE("recipe::parse errors on function in options") {
  sol::state lua;
  // lua_eval succeeds (functions become placeholders), but parse rejects them
  auto lua_val{ lua_eval(R"(
    result = {
      recipe = 'arm.gcc@v2',
      source = '/fake/r.lua',
      options = { func = function() return 42 end }
    }
  )", lua) };
  CHECK_THROWS_WITH_AS(envy::recipe_spec::parse(lua_val, fs::current_path()),
                       "Unsupported Lua type: function",
                       std::runtime_error);
}

TEST_CASE("recipe::parse errors on function nested in options") {
  sol::state lua;
  // lua_eval succeeds (functions become placeholders), but parse rejects them
  auto lua_val{ lua_eval(R"(
    result = {
      recipe = 'arm.gcc@v2',
      source = '/fake/r.lua',
      options = {
        compiler = {
          version = '13.2',
          callback = function() return true end
        }
      }
    }
  )", lua) };
  CHECK_THROWS_WITH_AS(envy::recipe_spec::parse(lua_val, fs::current_path()),
                       "Unsupported Lua type: function",
                       std::runtime_error);
}
