#include "lua_envy_options.h"

#include "lua_envy.h"
#include "sol_util.h"

#include "doctest.h"

namespace envy {
namespace {

struct lua_fixture {
  sol::state lua;
  sol::table schema;

  lua_fixture() {
    lua.open_libraries(sol::lib::base, sol::lib::string);
    sol::table envy_table{ lua.create_table() };
    lua_envy_options_install(envy_table);
    lua["envy"] = envy_table;
    schema = lua.create_table();
  }

  sol::object make_opts(std::string const &lua_expr) {
    auto result{ lua.safe_script("return " + lua_expr, sol::script_pass_on_error) };
    REQUIRE(result.valid());
    sol::object obj{ result };
    lua.registry()[ENVY_OPTIONS_RIDX] = obj;
    return obj;
  }

  sol::object nil_opts() {
    sol::object obj{ sol::lua_nil };
    lua.registry()[ENVY_OPTIONS_RIDX] = sol::lua_nil;
    return obj;
  }
};

// -- Table form: required --

TEST_CASE("OPTIONS: required option present succeeds") {
  lua_fixture f;
  sol::table constraint{ f.lua.create_table() };
  constraint["required"] = true;
  f.schema["version"] = constraint;

  sol::object opts{ f.make_opts("{ version = \"1.0\" }") };
  CHECK_NOTHROW(validate_options_schema(f.schema, opts, "test@v1"));
}

TEST_CASE("OPTIONS: required option missing throws") {
  lua_fixture f;
  sol::table constraint{ f.lua.create_table() };
  constraint["required"] = true;
  f.schema["version"] = constraint;

  sol::object opts{ f.make_opts("{}") };
  CHECK_THROWS_WITH(validate_options_schema(f.schema, opts, "test@v1"),
                    doctest::Contains("'version' is required"));
}

TEST_CASE("OPTIONS: required false and missing succeeds") {
  lua_fixture f;
  sol::table constraint{ f.lua.create_table() };
  constraint["required"] = false;
  f.schema["version"] = constraint;

  sol::object opts{ f.make_opts("{}") };
  CHECK_NOTHROW(validate_options_schema(f.schema, opts, "test@v1"));
}

TEST_CASE("OPTIONS: optional option missing succeeds") {
  lua_fixture f;
  sol::table constraint{ f.lua.create_table() };
  f.schema["version"] = constraint;

  sol::object opts{ f.make_opts("{}") };
  CHECK_NOTHROW(validate_options_schema(f.schema, opts, "test@v1"));
}

// -- Table form: unknown options --

TEST_CASE("OPTIONS: unknown option rejected") {
  lua_fixture f;
  sol::table constraint{ f.lua.create_table() };
  f.schema["version"] = constraint;

  sol::object opts{ f.make_opts("{ version = \"1\", foo = \"bar\" }") };
  CHECK_THROWS_WITH(validate_options_schema(f.schema, opts, "test@v1"),
                    doctest::Contains("unknown option 'foo'"));
}

TEST_CASE("OPTIONS: no opts with empty schema succeeds") {
  lua_fixture f;
  sol::object opts{ f.make_opts("{}") };
  CHECK_NOTHROW(validate_options_schema(f.schema, opts, "test@v1"));
}

TEST_CASE("OPTIONS: extra option with no schema keys") {
  lua_fixture f;
  sol::object opts{ f.make_opts("{ x = 1 }") };
  CHECK_THROWS_WITH(validate_options_schema(f.schema, opts, "test@v1"),
                    doctest::Contains("unknown option 'x'"));
}

// -- Table form: semver --

TEST_CASE("OPTIONS: semver valid string") {
  lua_fixture f;
  sol::table constraint{ f.lua.create_table() };
  constraint["semver"] = true;
  f.schema["version"] = constraint;

  sol::object opts{ f.make_opts("{ version = \"1.2.3\" }") };
  CHECK_NOTHROW(validate_options_schema(f.schema, opts, "test@v1"));
}

TEST_CASE("OPTIONS: semver valid with prerelease") {
  lua_fixture f;
  sol::table constraint{ f.lua.create_table() };
  constraint["semver"] = true;
  f.schema["version"] = constraint;

  sol::object opts{ f.make_opts("{ version = \"1.2.3-rc.1\" }") };
  CHECK_NOTHROW(validate_options_schema(f.schema, opts, "test@v1"));
}

TEST_CASE("OPTIONS: semver invalid string") {
  lua_fixture f;
  sol::table constraint{ f.lua.create_table() };
  constraint["semver"] = true;
  f.schema["version"] = constraint;

  sol::object opts{ f.make_opts("{ version = \"not.a.version\" }") };
  CHECK_THROWS_WITH(validate_options_schema(f.schema, opts, "test@v1"),
                    doctest::Contains("not valid semver"));
}

TEST_CASE("OPTIONS: semver rejects number") {
  lua_fixture f;
  sol::table constraint{ f.lua.create_table() };
  constraint["semver"] = true;
  f.schema["version"] = constraint;

  sol::object opts{ f.make_opts("{ version = 123 }") };
  CHECK_THROWS_WITH(validate_options_schema(f.schema, opts, "test@v1"),
                    doctest::Contains("not valid semver"));
}

TEST_CASE("OPTIONS: semver not required and missing") {
  lua_fixture f;
  sol::table constraint{ f.lua.create_table() };
  constraint["semver"] = true;
  f.schema["version"] = constraint;

  sol::object opts{ f.make_opts("{}") };
  CHECK_NOTHROW(validate_options_schema(f.schema, opts, "test@v1"));
}

// -- Table form: semver range --

TEST_CASE("OPTIONS: semver range pass") {
  lua_fixture f;
  sol::table constraint{ f.lua.create_table() };
  constraint["semver"] = true;
  constraint["range"] = ">=1.0.0 <2.0.0";
  f.schema["version"] = constraint;

  sol::object opts{ f.make_opts("{ version = \"1.5.0\" }") };
  CHECK_NOTHROW(validate_options_schema(f.schema, opts, "test@v1"));
}

TEST_CASE("OPTIONS: semver range boundary inclusive") {
  lua_fixture f;
  sol::table constraint{ f.lua.create_table() };
  constraint["semver"] = true;
  constraint["range"] = ">=1.0.0 <2.0.0";
  f.schema["version"] = constraint;

  sol::object opts{ f.make_opts("{ version = \"1.0.0\" }") };
  CHECK_NOTHROW(validate_options_schema(f.schema, opts, "test@v1"));
}

TEST_CASE("OPTIONS: semver range boundary exclusive") {
  lua_fixture f;
  sol::table constraint{ f.lua.create_table() };
  constraint["semver"] = true;
  constraint["range"] = ">=1.0.0 <2.0.0";
  f.schema["version"] = constraint;

  sol::object opts{ f.make_opts("{ version = \"2.0.0\" }") };
  CHECK_THROWS_WITH(validate_options_schema(f.schema, opts, "test@v1"),
                    doctest::Contains("does not satisfy range"));
}

TEST_CASE("OPTIONS: semver range below") {
  lua_fixture f;
  sol::table constraint{ f.lua.create_table() };
  constraint["semver"] = true;
  constraint["range"] = ">=1.0.0 <2.0.0";
  f.schema["version"] = constraint;

  sol::object opts{ f.make_opts("{ version = \"0.9.0\" }") };
  CHECK_THROWS_WITH(validate_options_schema(f.schema, opts, "test@v1"),
                    doctest::Contains("does not satisfy range"));
}

TEST_CASE("OPTIONS: semver range invalid range string") {
  lua_fixture f;
  sol::table constraint{ f.lua.create_table() };
  constraint["semver"] = true;
  constraint["range"] = ">>>bad";
  f.schema["version"] = constraint;

  sol::object opts{ f.make_opts("{ version = \"1.0.0\" }") };
  CHECK_THROWS_WITH(validate_options_schema(f.schema, opts, "test@v1"),
                    doctest::Contains("invalid range"));
}

TEST_CASE("OPTIONS: semver range multiple constraints") {
  lua_fixture f;
  sol::table constraint{ f.lua.create_table() };
  constraint["semver"] = true;
  constraint["range"] = ">=1.0.0 <2.0.0 >=1.2.0";
  f.schema["version"] = constraint;

  sol::object opts{ f.make_opts("{ version = \"1.1.0\" }") };
  CHECK_THROWS_WITH(validate_options_schema(f.schema, opts, "test@v1"),
                    doctest::Contains("does not satisfy range"));
}

TEST_CASE("OPTIONS: semver range with equal") {
  lua_fixture f;
  sol::table constraint{ f.lua.create_table() };
  constraint["semver"] = true;
  constraint["range"] = "=1.5.0";
  f.schema["version"] = constraint;

  sol::object opts{ f.make_opts("{ version = \"1.5.0\" }") };
  CHECK_NOTHROW(validate_options_schema(f.schema, opts, "test@v1"));
}

TEST_CASE("OPTIONS: semver range with equal mismatch") {
  lua_fixture f;
  sol::table constraint{ f.lua.create_table() };
  constraint["semver"] = true;
  constraint["range"] = "=1.5.0";
  f.schema["version"] = constraint;

  sol::object opts{ f.make_opts("{ version = \"1.5.1\" }") };
  CHECK_THROWS_WITH(validate_options_schema(f.schema, opts, "test@v1"),
                    doctest::Contains("does not satisfy range"));
}

// -- Table form: numeric range --

TEST_CASE("OPTIONS: numeric range integer pass") {
  lua_fixture f;
  sol::table constraint{ f.lua.create_table() };
  constraint["range"] = ">=1 <10";
  f.schema["count"] = constraint;

  sol::object opts{ f.make_opts("{ count = 5 }") };
  CHECK_NOTHROW(validate_options_schema(f.schema, opts, "test@v1"));
}

TEST_CASE("OPTIONS: numeric range integer fail") {
  lua_fixture f;
  sol::table constraint{ f.lua.create_table() };
  constraint["range"] = ">=1 <10";
  f.schema["count"] = constraint;

  sol::object opts{ f.make_opts("{ count = 15 }") };
  CHECK_THROWS_WITH(validate_options_schema(f.schema, opts, "test@v1"),
                    doctest::Contains("does not satisfy range"));
}

TEST_CASE("OPTIONS: numeric range float pass") {
  lua_fixture f;
  sol::table constraint{ f.lua.create_table() };
  constraint["range"] = ">=0.0 <=1.0";
  f.schema["ratio"] = constraint;

  sol::object opts{ f.make_opts("{ ratio = 0.5 }") };
  CHECK_NOTHROW(validate_options_schema(f.schema, opts, "test@v1"));
}

TEST_CASE("OPTIONS: numeric range float fail") {
  lua_fixture f;
  sol::table constraint{ f.lua.create_table() };
  constraint["range"] = ">=0.0 <=1.0";
  f.schema["ratio"] = constraint;

  sol::object opts{ f.make_opts("{ ratio = 1.5 }") };
  CHECK_THROWS_WITH(validate_options_schema(f.schema, opts, "test@v1"),
                    doctest::Contains("does not satisfy range"));
}

TEST_CASE("OPTIONS: numeric range string-typed number pass") {
  lua_fixture f;
  sol::table constraint{ f.lua.create_table() };
  constraint["range"] = ">=1 <10";
  f.schema["count"] = constraint;

  sol::object opts{ f.make_opts("{ count = \"5\" }") };
  CHECK_NOTHROW(validate_options_schema(f.schema, opts, "test@v1"));
}

TEST_CASE("OPTIONS: numeric range non-numeric string rejects") {
  lua_fixture f;
  sol::table constraint{ f.lua.create_table() };
  constraint["range"] = ">=1 <10";
  f.schema["count"] = constraint;

  sol::object opts{ f.make_opts("{ count = \"abc\" }") };
  CHECK_THROWS_WITH(validate_options_schema(f.schema, opts, "test@v1"),
                    doctest::Contains("not numeric"));
}

TEST_CASE("OPTIONS: numeric range boundary inclusive") {
  lua_fixture f;
  sol::table constraint{ f.lua.create_table() };
  constraint["range"] = ">=5";
  f.schema["count"] = constraint;

  sol::object opts{ f.make_opts("{ count = 5 }") };
  CHECK_NOTHROW(validate_options_schema(f.schema, opts, "test@v1"));
}

TEST_CASE("OPTIONS: numeric range boundary exclusive") {
  lua_fixture f;
  sol::table constraint{ f.lua.create_table() };
  constraint["range"] = ">5";
  f.schema["count"] = constraint;

  sol::object opts{ f.make_opts("{ count = 5 }") };
  CHECK_THROWS_WITH(validate_options_schema(f.schema, opts, "test@v1"),
                    doctest::Contains("does not satisfy range"));
}

TEST_CASE("OPTIONS: numeric range invalid token") {
  lua_fixture f;
  sol::table constraint{ f.lua.create_table() };
  constraint["range"] = ">=abc";
  f.schema["count"] = constraint;

  sol::object opts{ f.make_opts("{ count = 5 }") };
  CHECK_THROWS_WITH(validate_options_schema(f.schema, opts, "test@v1"),
                    doctest::Contains("invalid numeric range token"));
}

// -- Table form: custom validate --

TEST_CASE("OPTIONS: validate function nil return") {
  lua_fixture f;
  f.lua.safe_script("function my_validator(v) end");
  sol::table constraint{ f.lua.create_table() };
  constraint["validate"] = f.lua["my_validator"];
  f.schema["mode"] = constraint;

  sol::object opts{ f.make_opts("{ mode = \"install\" }") };
  CHECK_NOTHROW(validate_options_schema(f.schema, opts, "test@v1"));
}

TEST_CASE("OPTIONS: validate function true return") {
  lua_fixture f;
  f.lua.safe_script("function my_validator(v) return true end");
  sol::table constraint{ f.lua.create_table() };
  constraint["validate"] = f.lua["my_validator"];
  f.schema["mode"] = constraint;

  sol::object opts{ f.make_opts("{ mode = \"install\" }") };
  CHECK_NOTHROW(validate_options_schema(f.schema, opts, "test@v1"));
}

TEST_CASE("OPTIONS: validate function string return") {
  lua_fixture f;
  f.lua.safe_script("function my_validator(v) return \"bad mode\" end");
  sol::table constraint{ f.lua.create_table() };
  constraint["validate"] = f.lua["my_validator"];
  f.schema["mode"] = constraint;

  sol::object opts{ f.make_opts("{ mode = \"install\" }") };
  CHECK_THROWS_WITH(validate_options_schema(f.schema, opts, "test@v1"),
                    doctest::Contains("validation failed: bad mode"));
}

TEST_CASE("OPTIONS: validate function false return") {
  lua_fixture f;
  f.lua.safe_script("function my_validator(v) return false end");
  sol::table constraint{ f.lua.create_table() };
  constraint["validate"] = f.lua["my_validator"];
  f.schema["mode"] = constraint;

  sol::object opts{ f.make_opts("{ mode = \"install\" }") };
  CHECK_THROWS_WITH(validate_options_schema(f.schema, opts, "test@v1"),
                    doctest::Contains("validation failed"));
}

TEST_CASE("OPTIONS: validate function lua error") {
  lua_fixture f;
  f.lua.safe_script("function my_validator(v) error(\"boom\") end");
  sol::table constraint{ f.lua.create_table() };
  constraint["validate"] = f.lua["my_validator"];
  f.schema["mode"] = constraint;

  sol::object opts{ f.make_opts("{ mode = \"install\" }") };
  CHECK_THROWS_WITH(validate_options_schema(f.schema, opts, "test@v1"),
                    doctest::Contains("boom"));
}

TEST_CASE("OPTIONS: validate function receives correct value") {
  lua_fixture f;
  f.lua.safe_script(
      "function my_validator(v) assert(v == \"expected\", \"wrong: \" .. tostring(v)) "
      "end");
  sol::table constraint{ f.lua.create_table() };
  constraint["validate"] = f.lua["my_validator"];
  f.schema["mode"] = constraint;

  sol::object opts{ f.make_opts("{ mode = \"expected\" }") };
  CHECK_NOTHROW(validate_options_schema(f.schema, opts, "test@v1"));
}

// -- Table form: combined constraints --

TEST_CASE("OPTIONS: required + semver + range all pass") {
  lua_fixture f;
  sol::table constraint{ f.lua.create_table() };
  constraint["required"] = true;
  constraint["semver"] = true;
  constraint["range"] = ">=1.0.0";
  f.schema["version"] = constraint;

  sol::object opts{ f.make_opts("{ version = \"2.0.0\" }") };
  CHECK_NOTHROW(validate_options_schema(f.schema, opts, "test@v1"));
}

TEST_CASE("OPTIONS: required + semver fails on semver") {
  lua_fixture f;
  sol::table constraint{ f.lua.create_table() };
  constraint["required"] = true;
  constraint["semver"] = true;
  constraint["range"] = ">=1.0.0";
  f.schema["version"] = constraint;

  sol::object opts{ f.make_opts("{ version = \"bad\" }") };
  CHECK_THROWS_WITH(validate_options_schema(f.schema, opts, "test@v1"),
                    doctest::Contains("not valid semver"));
}

TEST_CASE("OPTIONS: required + range + validate, validate runs after range") {
  lua_fixture f;
  f.lua.safe_script("function my_validator(v) return \"custom error\" end");
  sol::table constraint{ f.lua.create_table() };
  constraint["range"] = ">=1 <100";
  constraint["validate"] = f.lua["my_validator"];
  f.schema["count"] = constraint;

  sol::object opts{ f.make_opts("{ count = 5 }") };
  CHECK_THROWS_WITH(validate_options_schema(f.schema, opts, "test@v1"),
                    doctest::Contains("custom error"));
}

TEST_CASE("OPTIONS: multiple options all valid") {
  lua_fixture f;
  sol::table version_c{ f.lua.create_table() };
  version_c["required"] = true;
  sol::table mode_c{ f.lua.create_table() };
  f.schema["version"] = version_c;
  f.schema["mode"] = mode_c;

  sol::object opts{ f.make_opts("{ version = \"1\", mode = \"x\" }") };
  CHECK_NOTHROW(validate_options_schema(f.schema, opts, "test@v1"));
}

TEST_CASE("OPTIONS: multiple options one invalid") {
  lua_fixture f;
  sol::table version_c{ f.lua.create_table() };
  version_c["required"] = true;
  sol::table mode_c{ f.lua.create_table() };
  f.schema["version"] = version_c;
  f.schema["mode"] = mode_c;

  sol::object opts{ f.make_opts("{ mode = \"x\" }") };
  CHECK_THROWS_WITH(validate_options_schema(f.schema, opts, "test@v1"),
                    doctest::Contains("'version' is required"));
}

// -- Table form: schema errors --

TEST_CASE("OPTIONS: schema entry not a table") {
  lua_fixture f;
  f.schema["version"] = "bad";

  sol::object opts{ f.make_opts("{ version = \"1\" }") };
  CHECK_THROWS_WITH(validate_options_schema(f.schema, opts, "test@v1"),
                    doctest::Contains("must be a table"));
}

// -- Function form: return values (tested via run_options in phase_spec_fetch) --
// These test envy.options() from Lua

TEST_CASE("OPTIONS: envy.options() succeeds with valid opts") {
  lua_fixture f;
  f.make_opts("{ version = \"1.0\" }");

  f.lua.safe_script(R"lua(
    IDENTITY = "test@v1"
    envy.options({ version = { required = true } })
  )lua");
}

TEST_CASE("OPTIONS: envy.options() fails with missing required") {
  lua_fixture f;
  f.make_opts("{}");

  CHECK_THROWS_WITH(f.lua.safe_script(R"lua(
    IDENTITY = "test@v1"
    envy.options({ version = { required = true } })
  )lua"),
                    doctest::Contains("'version' is required"));
}

TEST_CASE("OPTIONS: envy.options() rejects unknown opts") {
  lua_fixture f;
  f.make_opts("{ version = \"1\", typo = \"x\" }");

  CHECK_THROWS_WITH(f.lua.safe_script(R"lua(
    IDENTITY = "test@v1"
    envy.options({ version = {} })
  )lua"),
                    doctest::Contains("unknown option 'typo'"));
}

TEST_CASE("OPTIONS: function does validation after envy.options") {
  lua_fixture f;
  f.make_opts("{ version = \"1.0\" }");

  CHECK_THROWS_WITH(f.lua.safe_script(R"lua(
    IDENTITY = "test@v1"
    envy.options({ version = { required = true } })
    error("post-schema error")
  )lua"),
                    doctest::Contains("post-schema error"));
}

TEST_CASE("OPTIONS: nil opts with no required fields succeeds") {
  lua_fixture f;
  f.nil_opts();

  f.lua.safe_script(R"lua(
    IDENTITY = "test@v1"
    envy.options({ version = {} })
  )lua");
}

}  // namespace
}  // namespace envy
