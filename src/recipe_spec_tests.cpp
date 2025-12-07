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
    sol::error err = result;
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

  auto const *cfg{ envy::recipe_spec::parse(lua_val, fs::path("/fake"), true) };

  CHECK(cfg->identity == "python");
  CHECK(cfg->is_weak_reference());
  CHECK(cfg->weak == nullptr);
}

TEST_CASE("recipe::parse allows weak dependency with fallback when enabled") {
  sol::state lua;
  auto lua_val{ lua_eval(
      "result = { recipe = 'python', weak = { recipe = 'vendor.python@r4', source = "
      "'/fake/python.lua' } }",
      lua) };

  auto const *cfg{ envy::recipe_spec::parse(lua_val, fs::path("/fake"), true) };

  CHECK(cfg->identity == "python");
  CHECK(cfg->is_weak_reference());
  REQUIRE(cfg->weak);
  CHECK(cfg->weak->identity == "vendor.python@r4");
  CHECK(std::holds_alternative<envy::recipe_spec::local_source>(cfg->weak->source));
}

TEST_CASE("recipe::parse parses table with remote source") {
  sol::state lua;
  auto lua_val{ lua_eval(
      "result = { recipe = 'arm.gcc@v2', source = 'https://example.com/gcc.lua', sha256 = "
      "'abc123' }",
      lua) };

  auto const *cfg{ envy::recipe_spec::parse(lua_val, fs::path("/fake")) };

  CHECK(cfg->identity == "arm.gcc@v2");

  auto const *remote{ std::get_if<envy::recipe_spec::remote_source>(&cfg->source) };
  REQUIRE(remote != nullptr);
  CHECK(remote->url == "https://example.com/gcc.lua");
  CHECK(remote->sha256 == "abc123");
}

TEST_CASE("recipe::parse parses table with local source") {
  sol::state lua;
  auto lua_val{
    lua_eval("result = { recipe = 'local.tool@v1', source = './recipes/tool.lua' }", lua)
  };

  auto const *cfg{ envy::recipe_spec::parse(lua_val, fs::path("/project/envy.lua")) };

  CHECK(cfg->identity == "local.tool@v1");

  auto const *local{ std::get_if<envy::recipe_spec::local_source>(&cfg->source) };
  REQUIRE(local != nullptr);
  CHECK(local->file_path == fs::path("/project/recipes/tool.lua"));
}

TEST_CASE("recipe::parse resolves relative file paths") {
  sol::state lua;
  auto lua_val{
    lua_eval("result = { recipe = 'local.tool@v1', source = '../sibling/tool.lua' }", lua)
  };

  auto const *cfg{ envy::recipe_spec::parse(lua_val, fs::path("/project/sub/envy.lua")) };

  auto const *local{ std::get_if<envy::recipe_spec::local_source>(&cfg->source) };
  REQUIRE(local != nullptr);
  CHECK(local->file_path == fs::path("/project/sibling/tool.lua"));
}

TEST_CASE("recipe::parse parses table with options") {
  sol::state lua;
  auto lua_val{ lua_eval(
      "result = { recipe = 'arm.gcc@v2', source = '/fake/r.lua', options = { version = "
      "'13.2.0', target = "
      "'arm-none-eabi' } }",
      lua) };

  auto const *cfg{ envy::recipe_spec::parse(lua_val, fs::path("/fake")) };

  CHECK(cfg->identity == "arm.gcc@v2");

  // Deserialize and check
  auto opts_result{ lua.safe_script("return " + cfg->serialized_options) };
  REQUIRE(opts_result.valid());
  sol::table opts = opts_result;
  CHECK(sol::object(opts["version"]).as<std::string>() == "13.2.0");
  CHECK(sol::object(opts["target"]).as<std::string>() == "arm-none-eabi");
}

TEST_CASE("recipe::parse parses table with empty options") {
  sol::state lua;
  auto lua_val{ lua_eval(
      "result = { recipe = 'arm.gcc@v2', source = '/fake/r.lua', options = {} }",
      lua) };

  auto const *cfg{ envy::recipe_spec::parse(lua_val, fs::path("/fake")) };

  CHECK(cfg->identity == "arm.gcc@v2");
  CHECK(cfg->serialized_options == "{}");
}

TEST_CASE("recipe::parse parses table with all fields") {
  sol::state lua;
  auto lua_val{ lua_eval(
      "result = { recipe = 'arm.gcc@v2', source = 'https://example.com/gcc.lua', sha256 = "
      "'abc123', "
      "options = { version = '13.2.0' } }",
      lua) };

  auto const *cfg{ envy::recipe_spec::parse(lua_val, fs::path("/fake")) };

  CHECK(cfg->identity == "arm.gcc@v2");

  auto const *remote{ std::get_if<envy::recipe_spec::remote_source>(&cfg->source) };
  REQUIRE(remote != nullptr);
  CHECK(remote->url == "https://example.com/gcc.lua");
  CHECK(remote->sha256 == "abc123");

  // Deserialize and check
  auto opts_result{ lua.safe_script("return " + cfg->serialized_options) };
  REQUIRE(opts_result.valid());
  sol::table opts = opts_result;
  CHECK(sol::object(opts["version"]).as<std::string>() == "13.2.0");
}

TEST_CASE("recipe::parse parses product dependency fields") {
  SUBCASE("strong product dependency") {
    sol::state lua;
    auto lua_val{ lua_eval(
        "result = { recipe = 'local.provider@v1', product = 'tool', source = "
        "'/fake/provider.lua' }",
        lua) };

    auto const *cfg{ envy::recipe_spec::parse(lua_val, fs::path("/fake"), true) };

    CHECK(cfg->product.has_value());
    CHECK(*cfg->product == "tool");
    CHECK_FALSE(cfg->is_weak_reference());
  }

  SUBCASE("weak product dependency with fallback") {
    sol::state lua;
    auto lua_val{ lua_eval(
        "result = { recipe = 'local.consumer@v1', product = 'tool', weak = { recipe = "
        "'vendor.tool@v1', source = '/fake/tool.lua' } }",
        lua) };

    auto const *cfg{ envy::recipe_spec::parse(lua_val, fs::path("/fake"), true) };

    CHECK(cfg->product.has_value());
    CHECK(cfg->is_weak_reference());
    REQUIRE(cfg->weak);
    CHECK_FALSE(cfg->weak->product.has_value());
  }

  SUBCASE("ref-only product dependency unconstrained") {
    sol::state lua;
    auto lua_val{ lua_eval("result = { product = 'tool' }",  // No recipe field
                           lua) };

    auto const *cfg{ envy::recipe_spec::parse(lua_val, fs::path("/fake"), true) };

    CHECK(cfg->product.has_value());
    CHECK(*cfg->product == "tool");
    CHECK(cfg->identity.empty());  // Empty identity means unconstrained
    CHECK(cfg->is_weak_reference());
    CHECK(cfg->weak == nullptr);
  }

  SUBCASE("ref-only product dependency constrained") {
    sol::state lua;
    auto lua_val{ lua_eval("result = { recipe = 'local.consumer@v1', product = 'tool' }",
                           lua) };

    auto const *cfg{ envy::recipe_spec::parse(lua_val, fs::path("/fake"), true) };

    CHECK(cfg->product.has_value());
    CHECK(*cfg->product == "tool");
    CHECK(cfg->identity == "local.consumer@v1");  // Constraint identity
    CHECK(cfg->is_weak_reference());
    CHECK(cfg->weak == nullptr);
  }

  SUBCASE("rejects non-string product") {
    sol::state lua;
    auto lua_val{ lua_eval(
        "result = { recipe = 'foo@v1', product = 42, source = '/fake/foo.lua' }",
        lua) };

    CHECK_THROWS_WITH_AS(envy::recipe_spec::parse(lua_val, fs::path("/fake")),
                         doctest::Contains("product"),
                         std::runtime_error);
  }

  SUBCASE("rejects empty product") {
    sol::state lua;
    auto lua_val{ lua_eval(
        "result = { recipe = 'foo@v1', product = '', source = '/fake/foo.lua' }",
        lua) };

    CHECK_THROWS_WITH_AS(envy::recipe_spec::parse(lua_val, fs::path("/fake")),
                         doctest::Contains("cannot be empty"),
                         std::runtime_error);
  }
}

// Error cases ----------------------------------------------------------------

TEST_CASE("recipe::parse errors on invalid identity format") {
  sol::state lua;
  auto lua_val{
    lua_eval("result = { recipe = 'invalid-no-at-sign', source = '/fake/r.lua' }", lua)
  };

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
  auto lua_val{ lua_eval("result = { recipe = 'arm.gcc@', source = '/fake/r.lua' }",
                         lua) };

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
  auto lua_val{ lua_eval("result = { recipe = 'armgcc@v2', source = '/fake/r.lua' }",
                         lua) };

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
      "result = { recipe = 'arm.gcc@v2', source = 'https://example.com/gcc.lua' }",
      lua) };

  auto const *spec{ envy::recipe_spec::parse(lua_val, fs::path("/fake")) };
  CHECK(spec->identity == "arm.gcc@v2");
  CHECK(spec->is_remote());
  auto const *remote{ std::get_if<envy::recipe_spec::remote_source>(&spec->source) };
  REQUIRE(remote != nullptr);
  CHECK(remote->url == "https://example.com/gcc.lua");
  CHECK(remote->sha256.empty());  // No SHA256 provided (permissive)
}

TEST_CASE("recipe::parse errors on non-string source") {
  sol::state lua;
  auto lua_val{
    lua_eval("result = { recipe = 'arm.gcc@v2', source = 123, sha256 = 'abc' }", lua)
  };

  CHECK_THROWS_WITH_AS(envy::recipe_spec::parse(lua_val, fs::path("/fake")),
                       "Recipe 'source' field must be string or table",
                       std::runtime_error);
}

TEST_CASE("recipe::parse errors on non-string sha256") {
  sol::state lua;
  auto lua_val{ lua_eval(
      "result = { recipe = 'arm.gcc@v2', source = 'https://example.com/gcc.lua', sha256 = "
      "123 "
      "}",
      lua) };

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
      "}",
      lua) };

  CHECK_THROWS_WITH_AS(envy::recipe_spec::parse(lua_val, fs::path("/fake")),
                       "Recipe 'options' field must be table",
                       std::runtime_error);
}

TEST_CASE("recipe::parse accepts non-string option values") {
  sol::state lua;
  auto lua_val{ lua_eval(
      "result = { recipe = 'arm.gcc@v2', source = '/fake/r.lua', options = { version = "
      "123, "
      "debug = true, nested = { key = 'value' } } }",
      lua) };

  auto const *cfg{ envy::recipe_spec::parse(lua_val, fs::path("/fake")) };

  // Deserialize and check
  auto opts_result{ lua.safe_script("return " + cfg->serialized_options) };
  REQUIRE(opts_result.valid());
  sol::table opts = opts_result;
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
  )",
                         lua) };
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
  )",
                         lua) };
  CHECK_THROWS_WITH_AS(envy::recipe_spec::parse(lua_val, fs::current_path()),
                       "Unsupported Lua type: function",
                       std::runtime_error);
}

TEST_CASE("recipe::parse serializes simple string array") {
  sol::state lua;
  auto lua_val{ lua_eval(
      R"(result = { recipe = 'brew.pkg@r0', source = '/fake/r.lua',
                    options = { packages = { "neovim", "bat", "pv" } } })",
      lua) };

  auto const *cfg{ envy::recipe_spec::parse(lua_val, fs::path("/fake")) };

  // Deserialize and check
  auto opts_result{ lua.safe_script("return " + cfg->serialized_options) };
  REQUIRE(opts_result.valid());
  sol::table opts = opts_result;
  sol::table packages = opts["packages"];
  REQUIRE(packages.size() == 3);
  CHECK(packages[1].get<std::string>() == "neovim");
  CHECK(packages[2].get<std::string>() == "bat");
  CHECK(packages[3].get<std::string>() == "pv");
}

TEST_CASE("recipe::parse serializes integer array") {
  sol::state lua;
  auto lua_val{ lua_eval(
      R"(result = { recipe = 'local.test@r0', source = '/fake/r.lua',
                    options = { ports = { 8080, 8081, 8082 } } })",
      lua) };

  auto const *cfg{ envy::recipe_spec::parse(lua_val, fs::path("/fake")) };

  auto opts_result{ lua.safe_script("return " + cfg->serialized_options) };
  REQUIRE(opts_result.valid());
  sol::table opts = opts_result;
  sol::table ports = opts["ports"];
  REQUIRE(ports.size() == 3);
  CHECK(ports[1].get<lua_Integer>() == 8080);
  CHECK(ports[2].get<lua_Integer>() == 8081);
  CHECK(ports[3].get<lua_Integer>() == 8082);
}

TEST_CASE("recipe::parse serializes mixed-type array") {
  sol::state lua;
  auto lua_val{ lua_eval(
      R"(result = { recipe = 'local.test@r0', source = '/fake/r.lua',
                    options = { mixed = { "str", 42, true, "end" } } })",
      lua) };

  auto const *cfg{ envy::recipe_spec::parse(lua_val, fs::path("/fake")) };

  auto opts_result{ lua.safe_script("return " + cfg->serialized_options) };
  REQUIRE(opts_result.valid());
  sol::table opts = opts_result;
  sol::table mixed = opts["mixed"];
  REQUIRE(mixed.size() == 4);
  CHECK(mixed[1].get<std::string>() == "str");
  CHECK(mixed[2].get<lua_Integer>() == 42);
  CHECK(mixed[3].get<bool>() == true);
  CHECK(mixed[4].get<std::string>() == "end");
}

TEST_CASE("recipe::parse serializes float array") {
  sol::state lua;
  auto lua_val{ lua_eval(
      R"(result = { recipe = 'local.test@r0', source = '/fake/r.lua',
                    options = { values = { 1.5, 2.5, 3.5 } } })",
      lua) };

  auto const *cfg{ envy::recipe_spec::parse(lua_val, fs::path("/fake")) };

  auto opts_result{ lua.safe_script("return " + cfg->serialized_options) };
  REQUIRE(opts_result.valid());
  sol::table opts = opts_result;
  sol::table values = opts["values"];
  REQUIRE(values.size() == 3);
  CHECK(values[1].get<lua_Number>() == doctest::Approx(1.5));
  CHECK(values[2].get<lua_Number>() == doctest::Approx(2.5));
  CHECK(values[3].get<lua_Number>() == doctest::Approx(3.5));
}

TEST_CASE("recipe::parse preserves array order") {
  sol::state lua;
  auto lua_val{ lua_eval(
      R"(result = { recipe = 'local.test@r0', source = '/fake/r.lua',
                    options = { items = { "z", "a", "m", "b" } } })",
      lua) };

  auto const *cfg{ envy::recipe_spec::parse(lua_val, fs::path("/fake")) };

  auto opts_result{ lua.safe_script("return " + cfg->serialized_options) };
  REQUIRE(opts_result.valid());
  sol::table opts = opts_result;
  sol::table items = opts["items"];
  REQUIRE(items.size() == 4);
  // Verify array maintains order, not sorted lexicographically
  CHECK(items[1].get<std::string>() == "z");
  CHECK(items[2].get<std::string>() == "a");
  CHECK(items[3].get<std::string>() == "m");
  CHECK(items[4].get<std::string>() == "b");
}

TEST_CASE("recipe::parse serializes nested arrays") {
  sol::state lua;
  auto lua_val{ lua_eval(
      R"(result = { recipe = 'local.test@r0', source = '/fake/r.lua',
                    options = { matrix = { { 1, 2 }, { 3, 4 } } } })",
      lua) };

  auto const *cfg{ envy::recipe_spec::parse(lua_val, fs::path("/fake")) };

  auto opts_result{ lua.safe_script("return " + cfg->serialized_options) };
  REQUIRE(opts_result.valid());
  sol::table opts = opts_result;
  sol::table matrix = opts["matrix"];
  REQUIRE(matrix.size() == 2);
  sol::table row1 = matrix[1];
  sol::table row2 = matrix[2];
  CHECK(row1[1].get<lua_Integer>() == 1);
  CHECK(row1[2].get<lua_Integer>() == 2);
  CHECK(row2[1].get<lua_Integer>() == 3);
  CHECK(row2[2].get<lua_Integer>() == 4);
}

TEST_CASE("recipe::parse serializes table containing arrays") {
  sol::state lua;
  auto lua_val{ lua_eval(
      R"(result = { recipe = 'local.test@r0', source = '/fake/r.lua',
                    options = { config = { flags = { "-Wall", "-O2" }, level = 3 } } })",
      lua) };

  auto const *cfg{ envy::recipe_spec::parse(lua_val, fs::path("/fake")) };

  auto opts_result{ lua.safe_script("return " + cfg->serialized_options) };
  REQUIRE(opts_result.valid());
  sol::table opts = opts_result;
  sol::table config = opts["config"];
  sol::table flags = config["flags"];
  REQUIRE(flags.size() == 2);
  CHECK(flags[1].get<std::string>() == "-Wall");
  CHECK(flags[2].get<std::string>() == "-O2");
  CHECK(config["level"].get<lua_Integer>() == 3);
}

TEST_CASE("recipe::parse serializes array containing tables") {
  sol::state lua;
  auto lua_val{ lua_eval(
      R"(result = { recipe = 'local.test@r0', source = '/fake/r.lua',
                    options = { items = { { name = "foo" }, { name = "bar" } } } })",
      lua) };

  auto const *cfg{ envy::recipe_spec::parse(lua_val, fs::path("/fake")) };

  auto opts_result{ lua.safe_script("return " + cfg->serialized_options) };
  REQUIRE(opts_result.valid());
  sol::table opts = opts_result;
  sol::table items = opts["items"];
  REQUIRE(items.size() == 2);
  sol::table item1 = items[1];
  sol::table item2 = items[2];
  CHECK(item1["name"].get<std::string>() == "foo");
  CHECK(item2["name"].get<std::string>() == "bar");
}

TEST_CASE("recipe::parse serializes sparse table as table not array") {
  sol::state lua;
  auto lua_val{ lua_eval(
      R"(result = { recipe = 'local.test@r0', source = '/fake/r.lua',
                    options = { sparse = { [1] = "a", [3] = "c" } } })",
      lua) };

  auto const *cfg{ envy::recipe_spec::parse(lua_val, fs::path("/fake")) };

  auto opts_result{ lua.safe_script("return " + cfg->serialized_options) };
  REQUIRE(opts_result.valid());
  sol::table opts = opts_result;
  sol::table sparse = opts["sparse"];
  // Sparse table should not serialize as array - only string keys preserved
  // Since numeric keys aren't strings, they'll be dropped by current table logic
  CHECK(sparse.size() == 0);
}

TEST_CASE("recipe::parse serializes single-element array") {
  sol::state lua;
  auto lua_val{ lua_eval(
      R"(result = { recipe = 'local.test@r0', source = '/fake/r.lua',
                    options = { singleton = { "only" } } })",
      lua) };

  auto const *cfg{ envy::recipe_spec::parse(lua_val, fs::path("/fake")) };

  auto opts_result{ lua.safe_script("return " + cfg->serialized_options) };
  REQUIRE(opts_result.valid());
  sol::table opts = opts_result;
  sol::table singleton = opts["singleton"];
  REQUIRE(singleton.size() == 1);
  CHECK(singleton[1].get<std::string>() == "only");
}

TEST_CASE("recipe::parse serializes complex real-world options") {
  sol::state lua;
  auto lua_val{ lua_eval(
      R"(result = { recipe = 'brew.pkg@r0', source = '/fake/r.lua',
                    options = {
                      packages = { "ghostty", "neovim", "pv", "bat" },
                      version = "1.2.3",
                      flags = { debug = true, optimize = false }
                    } })",
      lua) };

  auto const *cfg{ envy::recipe_spec::parse(lua_val, fs::path("/fake")) };

  auto opts_result{ lua.safe_script("return " + cfg->serialized_options) };
  REQUIRE(opts_result.valid());
  sol::table opts = opts_result;

  sol::table packages = opts["packages"];
  REQUIRE(packages.size() == 4);
  CHECK(packages[1].get<std::string>() == "ghostty");
  CHECK(packages[2].get<std::string>() == "neovim");
  CHECK(packages[3].get<std::string>() == "pv");
  CHECK(packages[4].get<std::string>() == "bat");

  CHECK(opts["version"].get<std::string>() == "1.2.3");

  sol::table flags = opts["flags"];
  CHECK(flags["debug"].get<bool>() == true);
  CHECK(flags["optimize"].get<bool>() == false);
}
