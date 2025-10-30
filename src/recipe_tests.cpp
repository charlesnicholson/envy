#include "recipe.h"

#include "doctest.h"
#include "lua_util.h"

#include <filesystem>

namespace {

namespace fs = std::filesystem;

// Helper to parse a Lua string into a lua_value
envy::lua_value lua_eval(char const *script) {
  auto state{ envy::lua_make() };
  if (!state) { throw std::runtime_error("Failed to create Lua state"); }

  if (!envy::lua_run_string(state, script)) {
    throw std::runtime_error("Failed to execute Lua script");
  }

  auto result{ envy::lua_global_to_value(state.get(), "result") };
  if (!result) { throw std::runtime_error("No 'result' global found"); }

  return *result;
}

}  // namespace

TEST_CASE("recipe::cfg::parse parses simple string identity") {
  auto lua_val{ lua_eval("result = 'arm.gcc@v2'") };

  auto const cfg{ envy::recipe::cfg::parse(lua_val, fs::path("/fake")) };

  CHECK(cfg.identity == "arm.gcc@v2");
  CHECK(std::holds_alternative<envy::recipe::cfg::builtin_source>(cfg.source));
  CHECK(cfg.options.empty());
}

TEST_CASE("recipe::cfg::parse parses table with recipe field") {
  auto lua_val{ lua_eval("result = { recipe = 'gnu.binutils@v3' }") };

  auto const cfg{ envy::recipe::cfg::parse(lua_val, fs::path("/fake")) };

  CHECK(cfg.identity == "gnu.binutils@v3");
  CHECK(std::holds_alternative<envy::recipe::cfg::builtin_source>(cfg.source));
  CHECK(cfg.options.empty());
}

TEST_CASE("recipe::cfg::parse parses table with remote source") {
  auto lua_val{ lua_eval(
      "result = { recipe = 'arm.gcc@v2', url = 'https://example.com/gcc.lua', sha256 = "
      "'abc123' }") };

  auto const cfg{ envy::recipe::cfg::parse(lua_val, fs::path("/fake")) };

  CHECK(cfg.identity == "arm.gcc@v2");

  auto const *remote{ std::get_if<envy::recipe::cfg::remote_source>(&cfg.source) };
  REQUIRE(remote != nullptr);
  CHECK(remote->url == "https://example.com/gcc.lua");
  CHECK(remote->sha256 == "abc123");
}

TEST_CASE("recipe::cfg::parse parses table with local source") {
  auto lua_val{ lua_eval(
      "result = { recipe = 'local.tool@v1', file = './recipes/tool.lua' }") };

  auto const cfg{ envy::recipe::cfg::parse(lua_val, fs::path("/project/envy.lua")) };

  CHECK(cfg.identity == "local.tool@v1");

  auto const *local{ std::get_if<envy::recipe::cfg::local_source>(&cfg.source) };
  REQUIRE(local != nullptr);
  CHECK(local->file_path == fs::path("/project/recipes/tool.lua"));
}

TEST_CASE("recipe::cfg::parse resolves relative file paths") {
  auto lua_val{ lua_eval(
      "result = { recipe = 'local.tool@v1', file = '../sibling/tool.lua' }") };

  auto const cfg{ envy::recipe::cfg::parse(lua_val, fs::path("/project/sub/envy.lua")) };

  auto const *local{ std::get_if<envy::recipe::cfg::local_source>(&cfg.source) };
  REQUIRE(local != nullptr);
  CHECK(local->file_path == fs::path("/project/sibling/tool.lua"));
}

TEST_CASE("recipe::cfg::parse parses table with options") {
  auto lua_val{ lua_eval(
      "result = { recipe = 'arm.gcc@v2', options = { version = '13.2.0', target = "
      "'arm-none-eabi' } }") };

  auto const cfg{ envy::recipe::cfg::parse(lua_val, fs::path("/fake")) };

  CHECK(cfg.identity == "arm.gcc@v2");
  REQUIRE(cfg.options.size() == 2);
  CHECK(cfg.options.at("version") == "13.2.0");
  CHECK(cfg.options.at("target") == "arm-none-eabi");
}

TEST_CASE("recipe::cfg::parse parses table with empty options") {
  auto lua_val{ lua_eval("result = { recipe = 'arm.gcc@v2', options = {} }") };

  auto const cfg{ envy::recipe::cfg::parse(lua_val, fs::path("/fake")) };

  CHECK(cfg.identity == "arm.gcc@v2");
  CHECK(cfg.options.empty());
}

TEST_CASE("recipe::cfg::parse parses table with all fields") {
  auto lua_val{ lua_eval(
      "result = { recipe = 'arm.gcc@v2', url = 'https://example.com/gcc.lua', sha256 = "
      "'abc123', "
      "options = { version = '13.2.0' } }") };

  auto const cfg{ envy::recipe::cfg::parse(lua_val, fs::path("/fake")) };

  CHECK(cfg.identity == "arm.gcc@v2");

  auto const *remote{ std::get_if<envy::recipe::cfg::remote_source>(&cfg.source) };
  REQUIRE(remote != nullptr);
  CHECK(remote->url == "https://example.com/gcc.lua");
  CHECK(remote->sha256 == "abc123");

  REQUIRE(cfg.options.size() == 1);
  CHECK(cfg.options.at("version") == "13.2.0");
}

// Error cases ----------------------------------------------------------------

TEST_CASE("recipe::cfg::parse errors on invalid identity format") {
  auto lua_val{ lua_eval("result = 'invalid-no-at-sign'") };

  CHECK_THROWS_WITH_AS(envy::recipe::cfg::parse(lua_val, fs::path("/fake")),
                       "Invalid recipe identity format: invalid-no-at-sign",
                       std::runtime_error);
}

TEST_CASE("recipe::cfg::parse errors on identity missing namespace") {
  auto lua_val{ lua_eval("result = 'gcc@v2'") };

  CHECK_THROWS_WITH_AS(envy::recipe::cfg::parse(lua_val, fs::path("/fake")),
                       "Invalid recipe identity format: gcc@v2",
                       std::runtime_error);
}

TEST_CASE("recipe::cfg::parse errors on identity missing name") {
  auto lua_val{ lua_eval("result = 'arm.@v2'") };

  CHECK_THROWS_WITH_AS(envy::recipe::cfg::parse(lua_val, fs::path("/fake")),
                       "Invalid recipe identity format: arm.@v2",
                       std::runtime_error);
}

TEST_CASE("recipe::cfg::parse errors on identity missing version") {
  auto lua_val{ lua_eval("result = 'arm.gcc@'") };

  CHECK_THROWS_WITH_AS(envy::recipe::cfg::parse(lua_val, fs::path("/fake")),
                       "Invalid recipe identity format: arm.gcc@",
                       std::runtime_error);
}

TEST_CASE("recipe::cfg::parse errors on identity missing @ sign") {
  auto lua_val{ lua_eval("result = 'arm.gcc'") };

  CHECK_THROWS_WITH_AS(envy::recipe::cfg::parse(lua_val, fs::path("/fake")),
                       "Invalid recipe identity format: arm.gcc",
                       std::runtime_error);
}

TEST_CASE("recipe::cfg::parse errors on identity missing dot") {
  auto lua_val{ lua_eval("result = 'armgcc@v2'") };

  CHECK_THROWS_WITH_AS(envy::recipe::cfg::parse(lua_val, fs::path("/fake")),
                       "Invalid recipe identity format: armgcc@v2",
                       std::runtime_error);
}

TEST_CASE("recipe::cfg::parse errors on non-string and non-table value") {
  auto lua_val{ lua_eval("result = 123") };

  CHECK_THROWS_WITH_AS(envy::recipe::cfg::parse(lua_val, fs::path("/fake")),
                       "Recipe entry must be string or table",
                       std::runtime_error);
}

TEST_CASE("recipe::cfg::parse errors on table missing recipe field") {
  auto lua_val{ lua_eval("result = { url = 'https://example.com/foo.lua' }") };

  CHECK_THROWS_WITH_AS(envy::recipe::cfg::parse(lua_val, fs::path("/fake")),
                       "Recipe table missing required 'recipe' field",
                       std::runtime_error);
}

TEST_CASE("recipe::cfg::parse errors on non-string recipe field") {
  auto lua_val{ lua_eval("result = { recipe = 123 }") };

  CHECK_THROWS_WITH_AS(envy::recipe::cfg::parse(lua_val, fs::path("/fake")),
                       "Recipe 'recipe' field must be string",
                       std::runtime_error);
}

TEST_CASE("recipe::cfg::parse errors on both url and file") {
  auto lua_val{ lua_eval(
      "result = { recipe = 'arm.gcc@v2', url = 'https://example.com/gcc.lua', file = "
      "'./local.lua' }") };

  CHECK_THROWS_WITH_AS(envy::recipe::cfg::parse(lua_val, fs::path("/fake")),
                       "Recipe cannot specify both 'url' and 'file'",
                       std::runtime_error);
}

TEST_CASE("recipe::cfg::parse errors on url without sha256") {
  auto lua_val{ lua_eval(
      "result = { recipe = 'arm.gcc@v2', url = 'https://example.com/gcc.lua' }") };

  CHECK_THROWS_WITH_AS(envy::recipe::cfg::parse(lua_val, fs::path("/fake")),
                       "Recipe with 'url' must specify 'sha256'",
                       std::runtime_error);
}

TEST_CASE("recipe::cfg::parse errors on non-string url") {
  auto lua_val{ lua_eval(
      "result = { recipe = 'arm.gcc@v2', url = 123, sha256 = 'abc' }") };

  CHECK_THROWS_WITH_AS(envy::recipe::cfg::parse(lua_val, fs::path("/fake")),
                       "Recipe 'url' field must be string",
                       std::runtime_error);
}

TEST_CASE("recipe::cfg::parse errors on non-string sha256") {
  auto lua_val{ lua_eval(
      "result = { recipe = 'arm.gcc@v2', url = 'https://example.com/gcc.lua', sha256 = "
      "123 "
      "}") };

  CHECK_THROWS_WITH_AS(envy::recipe::cfg::parse(lua_val, fs::path("/fake")),
                       "Recipe 'sha256' field must be string",
                       std::runtime_error);
}

TEST_CASE("recipe::cfg::parse errors on non-string file") {
  auto lua_val{ lua_eval("result = { recipe = 'local.tool@v1', file = 123 }") };

  CHECK_THROWS_WITH_AS(envy::recipe::cfg::parse(lua_val, fs::path("/fake")),
                       "Recipe 'file' field must be string",
                       std::runtime_error);
}

TEST_CASE("recipe::cfg::parse errors on non-table options") {
  auto lua_val{ lua_eval("result = { recipe = 'arm.gcc@v2', options = 'not a table' }") };

  CHECK_THROWS_WITH_AS(envy::recipe::cfg::parse(lua_val, fs::path("/fake")),
                       "Recipe 'options' field must be table",
                       std::runtime_error);
}

TEST_CASE("recipe::cfg::parse errors on non-string option value") {
  auto lua_val{ lua_eval(
      "result = { recipe = 'arm.gcc@v2', options = { version = 123 } }") };

  CHECK_THROWS_WITH_AS(envy::recipe::cfg::parse(lua_val, fs::path("/fake")),
                       "Option value for 'version' must be string",
                       std::runtime_error);
}
