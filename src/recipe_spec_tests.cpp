#include "recipe_spec.h"

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

TEST_CASE("recipe::parse rejects string shorthand") {
  auto lua_val{ lua_eval("result = 'arm.gcc@v2'") };

  CHECK_THROWS_WITH_AS(envy::recipe_spec::parse(lua_val, fs::path("/fake")),
                       doctest::Contains("shorthand string syntax requires table"),
                       std::runtime_error);
}

TEST_CASE("recipe::parse rejects table without source") {
  auto lua_val{ lua_eval("result = { recipe = 'gnu.binutils@v3' }") };

  CHECK_THROWS_WITH_AS(envy::recipe_spec::parse(lua_val, fs::path("/fake")),
                       doctest::Contains("must specify 'source' field"),
                       std::runtime_error);
}

TEST_CASE("recipe::parse parses table with remote source") {
  auto lua_val{ lua_eval(
      "result = { recipe = 'arm.gcc@v2', source = 'https://example.com/gcc.lua', sha256 = "
      "'abc123' }") };

  auto const cfg{ envy::recipe_spec::parse(lua_val, fs::path("/fake")) };

  CHECK(cfg.identity == "arm.gcc@v2");

  auto const *remote{ std::get_if<envy::recipe_spec::remote_source>(&cfg.source) };
  REQUIRE(remote != nullptr);
  CHECK(remote->url == "https://example.com/gcc.lua");
  CHECK(remote->sha256 == "abc123");
}

TEST_CASE("recipe::parse parses table with local source") {
  auto lua_val{ lua_eval(
      "result = { recipe = 'local.tool@v1', source = './recipes/tool.lua' }") };

  auto const cfg{ envy::recipe_spec::parse(lua_val, fs::path("/project/envy.lua")) };

  CHECK(cfg.identity == "local.tool@v1");

  auto const *local{ std::get_if<envy::recipe_spec::local_source>(&cfg.source) };
  REQUIRE(local != nullptr);
  CHECK(local->file_path == fs::path("/project/recipes/tool.lua"));
}

TEST_CASE("recipe::parse resolves relative file paths") {
  auto lua_val{ lua_eval(
      "result = { recipe = 'local.tool@v1', source = '../sibling/tool.lua' }") };

  auto const cfg{ envy::recipe_spec::parse(lua_val, fs::path("/project/sub/envy.lua")) };

  auto const *local{ std::get_if<envy::recipe_spec::local_source>(&cfg.source) };
  REQUIRE(local != nullptr);
  CHECK(local->file_path == fs::path("/project/sibling/tool.lua"));
}

TEST_CASE("recipe::parse parses table with options") {
  auto lua_val{ lua_eval(
      "result = { recipe = 'arm.gcc@v2', source = '/fake/r.lua', options = { version = "
      "'13.2.0', target = "
      "'arm-none-eabi' } }") };

  auto const cfg{ envy::recipe_spec::parse(lua_val, fs::path("/fake")) };

  CHECK(cfg.identity == "arm.gcc@v2");
  REQUIRE(cfg.options.size() == 2);
  CHECK(*cfg.options.at("version").get<std::string>() == "13.2.0");
  CHECK(*cfg.options.at("target").get<std::string>() == "arm-none-eabi");
}

TEST_CASE("recipe::parse parses table with empty options") {
  auto lua_val{ lua_eval(
      "result = { recipe = 'arm.gcc@v2', source = '/fake/r.lua', options = {} }") };

  auto const cfg{ envy::recipe_spec::parse(lua_val, fs::path("/fake")) };

  CHECK(cfg.identity == "arm.gcc@v2");
  CHECK(cfg.options.empty());
}

TEST_CASE("recipe::parse parses table with all fields") {
  auto lua_val{ lua_eval(
      "result = { recipe = 'arm.gcc@v2', source = 'https://example.com/gcc.lua', sha256 = "
      "'abc123', "
      "options = { version = '13.2.0' } }") };

  auto const cfg{ envy::recipe_spec::parse(lua_val, fs::path("/fake")) };

  CHECK(cfg.identity == "arm.gcc@v2");

  auto const *remote{ std::get_if<envy::recipe_spec::remote_source>(&cfg.source) };
  REQUIRE(remote != nullptr);
  CHECK(remote->url == "https://example.com/gcc.lua");
  CHECK(remote->sha256 == "abc123");

  REQUIRE(cfg.options.size() == 1);
  CHECK(*cfg.options.at("version").get<std::string>() == "13.2.0");
}

// Error cases ----------------------------------------------------------------

TEST_CASE("recipe::parse errors on invalid identity format") {
  auto lua_val{ lua_eval(
      "result = { recipe = 'invalid-no-at-sign', source = '/fake/r.lua' }") };

  CHECK_THROWS_WITH_AS(envy::recipe_spec::parse(lua_val, fs::path("/fake")),
                       "Invalid recipe identity format: invalid-no-at-sign",
                       std::runtime_error);
}

TEST_CASE("recipe::parse errors on identity missing namespace") {
  auto lua_val{ lua_eval("result = { recipe = 'gcc@v2', source = '/fake/r.lua' }") };

  CHECK_THROWS_WITH_AS(envy::recipe_spec::parse(lua_val, fs::path("/fake")),
                       "Invalid recipe identity format: gcc@v2",
                       std::runtime_error);
}

TEST_CASE("recipe::parse errors on identity missing name") {
  auto lua_val{ lua_eval("result = { recipe = 'arm.@v2', source = '/fake/r.lua' }") };

  CHECK_THROWS_WITH_AS(envy::recipe_spec::parse(lua_val, fs::path("/fake")),
                       "Invalid recipe identity format: arm.@v2",
                       std::runtime_error);
}

TEST_CASE("recipe::parse errors on identity missing version") {
  auto lua_val{ lua_eval("result = { recipe = 'arm.gcc@', source = '/fake/r.lua' }") };

  CHECK_THROWS_WITH_AS(envy::recipe_spec::parse(lua_val, fs::path("/fake")),
                       "Invalid recipe identity format: arm.gcc@",
                       std::runtime_error);
}

TEST_CASE("recipe::parse errors on identity missing @ sign") {
  auto lua_val{ lua_eval("result = { recipe = 'arm.gcc', source = '/fake/r.lua' }") };

  CHECK_THROWS_WITH_AS(envy::recipe_spec::parse(lua_val, fs::path("/fake")),
                       "Invalid recipe identity format: arm.gcc",
                       std::runtime_error);
}

TEST_CASE("recipe::parse errors on identity missing dot") {
  auto lua_val{ lua_eval("result = { recipe = 'armgcc@v2', source = '/fake/r.lua' }") };

  CHECK_THROWS_WITH_AS(envy::recipe_spec::parse(lua_val, fs::path("/fake")),
                       "Invalid recipe identity format: armgcc@v2",
                       std::runtime_error);
}

TEST_CASE("recipe::parse errors on non-string and non-table value") {
  auto lua_val{ lua_eval("result = 123") };

  CHECK_THROWS_WITH_AS(envy::recipe_spec::parse(lua_val, fs::path("/fake")),
                       "Recipe entry must be string or table",
                       std::runtime_error);
}

TEST_CASE("recipe::parse errors on table missing recipe field") {
  auto lua_val{ lua_eval("result = { source = 'https://example.com/foo.lua' }") };

  CHECK_THROWS_WITH_AS(envy::recipe_spec::parse(lua_val, fs::path("/fake")),
                       "Recipe table missing required 'recipe' field",
                       std::runtime_error);
}

TEST_CASE("recipe::parse errors on non-string recipe field") {
  auto lua_val{ lua_eval("result = { recipe = 123 }") };

  CHECK_THROWS_WITH_AS(envy::recipe_spec::parse(lua_val, fs::path("/fake")),
                       "Recipe 'recipe' field must be string",
                       std::runtime_error);
}

// Test removed - can no longer specify both url and file since we unified to 'source'

TEST_CASE("recipe::parse allows url without sha256 (permissive mode)") {
  auto lua_val{ lua_eval(
      "result = { recipe = 'arm.gcc@v2', source = 'https://example.com/gcc.lua' }") };

  auto const spec{ envy::recipe_spec::parse(lua_val, fs::path("/fake")) };
  CHECK(spec.identity == "arm.gcc@v2");
  CHECK(spec.is_remote());
  auto const *remote{ std::get_if<envy::recipe_spec::remote_source>(&spec.source) };
  REQUIRE(remote != nullptr);
  CHECK(remote->url == "https://example.com/gcc.lua");
  CHECK(remote->sha256.empty());  // No SHA256 provided (permissive)
}

TEST_CASE("recipe::parse errors on non-string source") {
  auto lua_val{ lua_eval(
      "result = { recipe = 'arm.gcc@v2', source = 123, sha256 = 'abc' }") };

  CHECK_THROWS_WITH_AS(envy::recipe_spec::parse(lua_val, fs::path("/fake")),
                       "Recipe 'source' field must be string",
                       std::runtime_error);
}

TEST_CASE("recipe::parse errors on non-string sha256") {
  auto lua_val{ lua_eval(
      "result = { recipe = 'arm.gcc@v2', source = 'https://example.com/gcc.lua', sha256 = "
      "123 "
      "}") };

  CHECK_THROWS_WITH_AS(envy::recipe_spec::parse(lua_val, fs::path("/fake")),
                       "Recipe 'sha256' field must be string",
                       std::runtime_error);
}

TEST_CASE("recipe::parse errors on non-string source (local)") {
  auto lua_val{ lua_eval("result = { recipe = 'local.tool@v1', source = 123 }") };

  CHECK_THROWS_WITH_AS(envy::recipe_spec::parse(lua_val, fs::path("/fake")),
                       "Recipe 'source' field must be string",
                       std::runtime_error);
}

TEST_CASE("recipe::parse errors on non-table options") {
  auto lua_val{ lua_eval(
      "result = { recipe = 'arm.gcc@v2', source = '/fake/r.lua', options = 'not a table' "
      "}") };

  CHECK_THROWS_WITH_AS(envy::recipe_spec::parse(lua_val, fs::path("/fake")),
                       "Recipe 'options' field must be table",
                       std::runtime_error);
}

TEST_CASE("recipe::parse accepts non-string option values") {
  auto lua_val{ lua_eval(
      "result = { recipe = 'arm.gcc@v2', source = '/fake/r.lua', options = { version = 123, "
      "debug = true, nested = { key = 'value' } } }") };

  auto const cfg{ envy::recipe_spec::parse(lua_val, fs::path("/fake")) };

  REQUIRE(cfg.options.size() == 3);
  CHECK(cfg.options.at("version").is_integer());
  CHECK(*cfg.options.at("version").get<int64_t>() == 123);
  CHECK(cfg.options.at("debug").is_bool());
  CHECK(*cfg.options.at("debug").get<bool>() == true);
  CHECK(cfg.options.at("nested").is_table());
}

TEST_CASE("recipe::parse errors on function in options") {
  // Exception thrown during lua_eval when converting function to lua_value
  CHECK_THROWS_WITH_AS(lua_eval(R"(
    result = {
      recipe = 'arm.gcc@v2',
      source = '/fake/r.lua',
      options = { func = function() return 42 end }
    }
  )"),
                       "Unsupported Lua type: function",
                       std::runtime_error);
}

TEST_CASE("recipe::parse errors on function nested in options") {
  // Exception thrown during lua_eval when converting nested function to lua_value
  CHECK_THROWS_WITH_AS(lua_eval(R"(
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
  )"),
                       "Unsupported Lua type: function",
                       std::runtime_error);
}

// serialize_option_table tests -------------------------------------------

TEST_CASE("serialize_option_table serializes nil") {
  envy::lua_value val{};
  CHECK(envy::recipe_spec::serialize_option_table(val) == "nil");
}

TEST_CASE("serialize_option_table serializes bool true") {
  envy::lua_value val{ envy::lua_variant{ true } };
  CHECK(envy::recipe_spec::serialize_option_table(val) == "true");
}

TEST_CASE("serialize_option_table serializes bool false") {
  envy::lua_value val{ envy::lua_variant{ false } };
  CHECK(envy::recipe_spec::serialize_option_table(val) == "false");
}

TEST_CASE("serialize_option_table serializes positive integer") {
  envy::lua_value val{ envy::lua_variant{ int64_t{ 42 } } };
  CHECK(envy::recipe_spec::serialize_option_table(val) == "42");
}

TEST_CASE("serialize_option_table serializes negative integer") {
  envy::lua_value val{ envy::lua_variant{ int64_t{ -999 } } };
  CHECK(envy::recipe_spec::serialize_option_table(val) == "-999");
}

TEST_CASE("serialize_option_table serializes zero") {
  envy::lua_value val{ envy::lua_variant{ int64_t{ 0 } } };
  CHECK(envy::recipe_spec::serialize_option_table(val) == "0");
}

TEST_CASE("serialize_option_table serializes double") {
  envy::lua_value val{ envy::lua_variant{ 3.14159 } };
  auto const result{ envy::recipe_spec::serialize_option_table(val) };
  CHECK(std::stod(result) == doctest::Approx(3.14159));
}

TEST_CASE("serialize_option_table serializes double with shortest representation") {
  envy::lua_value val{ envy::lua_variant{ 1.5 } };
  CHECK(envy::recipe_spec::serialize_option_table(val) == "1.5");
}

TEST_CASE("serialize_option_table serializes simple string") {
  envy::lua_value val{ envy::lua_variant{ std::string{ "hello" } } };
  CHECK(envy::recipe_spec::serialize_option_table(val) == "\"hello\"");
}

TEST_CASE("serialize_option_table serializes empty string") {
  envy::lua_value val{ envy::lua_variant{ std::string{ "" } } };
  CHECK(envy::recipe_spec::serialize_option_table(val) == "\"\"");
}

TEST_CASE("serialize_option_table escapes quote in string") {
  envy::lua_value val{ envy::lua_variant{ std::string{ "say \"hello\"" } } };
  CHECK(envy::recipe_spec::serialize_option_table(val) == "\"say \\\"hello\\\"\"");
}

TEST_CASE("serialize_option_table escapes backslash in string") {
  envy::lua_value val{ envy::lua_variant{ std::string{ "path\\to\\file" } } };
  CHECK(envy::recipe_spec::serialize_option_table(val) == "\"path\\\\to\\\\file\"");
}

TEST_CASE("serialize_option_table escapes mixed quote and backslash") {
  envy::lua_value val{ envy::lua_variant{ std::string{ "\\\"escape\"\\me\\" } } };
  CHECK(envy::recipe_spec::serialize_option_table(val) == "\"\\\\\\\"escape\\\"\\\\me\\\\\"");
}

TEST_CASE("serialize_option_table serializes empty table") {
  envy::lua_table table;
  envy::lua_value val{ envy::lua_variant{ std::move(table) } };
  CHECK(envy::recipe_spec::serialize_option_table(val) == "{}");
}

TEST_CASE("serialize_option_table serializes single-entry table") {
  envy::lua_table table;
  table["key"] = envy::lua_value{ envy::lua_variant{ std::string{ "value" } } };
  envy::lua_value val{ envy::lua_variant{ std::move(table) } };
  CHECK(envy::recipe_spec::serialize_option_table(val) == "{key=\"value\"}");
}

TEST_CASE("serialize_option_table sorts table keys lexicographically") {
  envy::lua_table table;
  table["zebra"] = envy::lua_value{ envy::lua_variant{ int64_t{ 3 } } };
  table["apple"] = envy::lua_value{ envy::lua_variant{ int64_t{ 1 } } };
  table["middle"] = envy::lua_value{ envy::lua_variant{ int64_t{ 2 } } };
  envy::lua_value val{ envy::lua_variant{ std::move(table) } };
  CHECK(envy::recipe_spec::serialize_option_table(val) == "{apple=1,middle=2,zebra=3}");
}

TEST_CASE("serialize_option_table sorts keys case-sensitively") {
  envy::lua_table table;
  table["Zebra"] = envy::lua_value{ envy::lua_variant{ int64_t{ 1 } } };
  table["apple"] = envy::lua_value{ envy::lua_variant{ int64_t{ 2 } } };
  table["Banana"] = envy::lua_value{ envy::lua_variant{ int64_t{ 3 } } };
  envy::lua_value val{ envy::lua_variant{ std::move(table) } };
  // Uppercase letters come before lowercase in ASCII
  CHECK(envy::recipe_spec::serialize_option_table(val) == "{Banana=3,Zebra=1,apple=2}");
}

TEST_CASE("serialize_option_table serializes table with mixed types") {
  envy::lua_table table;
  table["bool"] = envy::lua_value{ envy::lua_variant{ true } };
  table["int"] = envy::lua_value{ envy::lua_variant{ int64_t{ 42 } } };
  table["str"] = envy::lua_value{ envy::lua_variant{ std::string{ "text" } } };
  table["nil"] = envy::lua_value{ envy::lua_variant{ std::monostate{} } };
  envy::lua_value val{ envy::lua_variant{ std::move(table) } };
  CHECK(envy::recipe_spec::serialize_option_table(val) == "{bool=true,int=42,nil=nil,str=\"text\"}");
}

TEST_CASE("serialize_option_table serializes nested table") {
  envy::lua_table inner;
  inner["nested"] = envy::lua_value{ envy::lua_variant{ std::string{ "value" } } };

  envy::lua_table outer;
  outer["outer"] = envy::lua_value{ envy::lua_variant{ std::move(inner) } };

  envy::lua_value val{ envy::lua_variant{ std::move(outer) } };
  CHECK(envy::recipe_spec::serialize_option_table(val) == "{outer={nested=\"value\"}}");
}

TEST_CASE("serialize_option_table sorts keys at each nesting level") {
  envy::lua_table inner;
  inner["z"] = envy::lua_value{ envy::lua_variant{ int64_t{ 2 } } };
  inner["a"] = envy::lua_value{ envy::lua_variant{ int64_t{ 1 } } };

  envy::lua_table outer;
  outer["y"] = envy::lua_value{ envy::lua_variant{ std::move(inner) } };
  outer["b"] = envy::lua_value{ envy::lua_variant{ int64_t{ 0 } } };

  envy::lua_value val{ envy::lua_variant{ std::move(outer) } };
  CHECK(envy::recipe_spec::serialize_option_table(val) == "{b=0,y={a=1,z=2}}");
}

TEST_CASE("serialize_option_table handles 3-level nested tables") {
  envy::lua_table level3;
  level3["deep"] = envy::lua_value{ envy::lua_variant{ std::string{ "bottom" } } };

  envy::lua_table level2;
  level2["l2"] = envy::lua_value{ envy::lua_variant{ std::move(level3) } };

  envy::lua_table level1;
  level1["l1"] = envy::lua_value{ envy::lua_variant{ std::move(level2) } };

  envy::lua_value val{ envy::lua_variant{ std::move(level1) } };
  CHECK(envy::recipe_spec::serialize_option_table(val) == "{l1={l2={deep=\"bottom\"}}}");
}

TEST_CASE("serialize_option_table handles 5-level nested tables with sorting") {
  // Build from innermost to outermost
  envy::lua_table l5;
  l5["z5"] = envy::lua_value{ envy::lua_variant{ int64_t{ 5 } } };
  l5["a5"] = envy::lua_value{ envy::lua_variant{ int64_t{ 5 } } };

  envy::lua_table l4;
  l4["z4"] = envy::lua_value{ envy::lua_variant{ int64_t{ 4 } } };
  l4["nest"] = envy::lua_value{ envy::lua_variant{ std::move(l5) } };
  l4["a4"] = envy::lua_value{ envy::lua_variant{ int64_t{ 4 } } };

  envy::lua_table l3;
  l3["z3"] = envy::lua_value{ envy::lua_variant{ std::move(l4) } };
  l3["a3"] = envy::lua_value{ envy::lua_variant{ int64_t{ 3 } } };

  envy::lua_table l2;
  l2["z2"] = envy::lua_value{ envy::lua_variant{ int64_t{ 2 } } };
  l2["nest"] = envy::lua_value{ envy::lua_variant{ std::move(l3) } };
  l2["a2"] = envy::lua_value{ envy::lua_variant{ int64_t{ 2 } } };

  envy::lua_table l1;
  l1["z1"] = envy::lua_value{ envy::lua_variant{ std::move(l2) } };
  l1["a1"] = envy::lua_value{ envy::lua_variant{ int64_t{ 1 } } };

  envy::lua_value val{ envy::lua_variant{ std::move(l1) } };
  // Keys sorted at every level: a before nest before z
  CHECK(envy::recipe_spec::serialize_option_table(val) ==
        "{a1=1,z1={a2=2,nest={a3=3,z3={a4=4,nest={a5=5,z5=5},z4=4}},z2=2}}");
}

TEST_CASE("serialize_option_table handles wide nested tables") {
  // Multiple siblings at each level
  envy::lua_table inner1;
  inner1["i1a"] = envy::lua_value{ envy::lua_variant{ int64_t{ 1 } } };
  inner1["i1z"] = envy::lua_value{ envy::lua_variant{ int64_t{ 2 } } };

  envy::lua_table inner2;
  inner2["i2m"] = envy::lua_value{ envy::lua_variant{ int64_t{ 3 } } };

  envy::lua_table inner3;
  inner3["i3x"] = envy::lua_value{ envy::lua_variant{ int64_t{ 4 } } };
  inner3["i3b"] = envy::lua_value{ envy::lua_variant{ int64_t{ 5 } } };

  envy::lua_table outer;
  outer["c"] = envy::lua_value{ envy::lua_variant{ std::move(inner2) } };
  outer["a"] = envy::lua_value{ envy::lua_variant{ std::move(inner1) } };
  outer["z"] = envy::lua_value{ envy::lua_variant{ std::move(inner3) } };
  outer["m"] = envy::lua_value{ envy::lua_variant{ int64_t{ 6 } } };

  envy::lua_value val{ envy::lua_variant{ std::move(outer) } };
  CHECK(envy::recipe_spec::serialize_option_table(val) ==
        "{a={i1a=1,i1z=2},c={i2m=3},m=6,z={i3b=5,i3x=4}}");
}

TEST_CASE("serialize_option_table disambiguates string with equals from nested table") {
  envy::lua_table table1;
  table1["a"] = envy::lua_value{ envy::lua_variant{ std::string{ "b=c" } } };
  envy::lua_value val1{ envy::lua_variant{ std::move(table1) } };
  auto const result1{ envy::recipe_spec::serialize_option_table(val1) };

  envy::lua_table inner;
  inner["b"] = envy::lua_value{ envy::lua_variant{ std::string{ "c" } } };
  envy::lua_table table2;
  table2["a"] = envy::lua_value{ envy::lua_variant{ std::move(inner) } };
  envy::lua_value val2{ envy::lua_variant{ std::move(table2) } };
  auto const result2{ envy::recipe_spec::serialize_option_table(val2) };

  CHECK(result1 == "{a=\"b=c\"}");
  CHECK(result2 == "{a={b=\"c\"}}");
  CHECK(result1 != result2);
}

TEST_CASE("serialize_option_table disambiguates string with braces from nested table") {
  envy::lua_table table1;
  table1["a"] = envy::lua_value{ envy::lua_variant{ std::string{ "b{c" } } };
  envy::lua_value val1{ envy::lua_variant{ std::move(table1) } };
  auto const result1{ envy::recipe_spec::serialize_option_table(val1) };

  envy::lua_table inner;
  inner["b"] = envy::lua_value{ envy::lua_variant{ std::string{ "c" } } };
  envy::lua_table table2;
  table2["a"] = envy::lua_value{ envy::lua_variant{ std::move(inner) } };
  envy::lua_value val2{ envy::lua_variant{ std::move(table2) } };
  auto const result2{ envy::recipe_spec::serialize_option_table(val2) };

  CHECK(result1 == "{a=\"b{c\"}");
  CHECK(result2 == "{a={b=\"c\"}}");
  CHECK(result1 != result2);
}

TEST_CASE("serialize_option_table handles complex nested options") {
  // Real-world: options = { arch = "arm64", compiler = { version = "13.2", flags = "-O2" }, debug = true }
  envy::lua_table compiler;
  compiler["flags"] = envy::lua_value{ envy::lua_variant{ std::string{ "-O2" } } };
  compiler["version"] = envy::lua_value{ envy::lua_variant{ std::string{ "13.2" } } };

  envy::lua_table options;
  options["arch"] = envy::lua_value{ envy::lua_variant{ std::string{ "arm64" } } };
  options["compiler"] = envy::lua_value{ envy::lua_variant{ std::move(compiler) } };
  options["debug"] = envy::lua_value{ envy::lua_variant{ true } };

  envy::lua_value val{ envy::lua_variant{ std::move(options) } };
  CHECK(envy::recipe_spec::serialize_option_table(val) ==
        "{arch=\"arm64\",compiler={flags=\"-O2\",version=\"13.2\"},debug=true}");
}

TEST_CASE("serialize_option_table mixed nesting with all types") {
  envy::lua_table features;
  features["ssl"] = envy::lua_value{ envy::lua_variant{ true } };
  features["threads"] = envy::lua_value{ envy::lua_variant{ int64_t{ 8 } } };
  features["timeout"] = envy::lua_value{ envy::lua_variant{ 30.5 } };

  envy::lua_table config;
  config["version"] = envy::lua_value{ envy::lua_variant{ std::string{ "2.0" } } };
  config["features"] = envy::lua_value{ envy::lua_variant{ std::move(features) } };
  config["nil_val"] = envy::lua_value{ envy::lua_variant{ std::monostate{} } };

  envy::lua_value val{ envy::lua_variant{ std::move(config) } };
  auto const result{ envy::recipe_spec::serialize_option_table(val) };

  // Verify features are sorted within nested table
  CHECK(result.find("features={ssl=true,threads=8,timeout=30.5}") != std::string::npos);
  // Verify outer keys are sorted
  CHECK(result.find("features=") < result.find("nil_val="));
  CHECK(result.find("nil_val=") < result.find("version="));
}
