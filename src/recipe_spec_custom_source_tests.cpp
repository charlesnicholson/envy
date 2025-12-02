#include "recipe_spec.h"

#include "doctest.h"
#include "sol/sol.hpp"
#include "sol_util.h"

#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {

// Helper: Set up a Lua environment simulating a recipe with dependencies
// Creates a dependencies global array with the given recipe table
void setup_recipe_environment(sol::state &lua,
                              std::string const &identity,
                              std::vector<std::string> const &dep_identities = {}) {
  // Build Lua code that creates a dependencies global
  std::string lua_code{ "dependencies = {\n  {\n    recipe = \"" + identity +
                        "\",\n    source = {\n" };

  if (!dep_identities.empty()) {
    lua_code += "      dependencies = {\n";
    for (auto const &dep_id : dep_identities) {
      lua_code += "        { recipe = \"" + dep_id + "\", source = \"file:///tmp/" +
                  dep_id + ".lua\" },\n";
    }
    lua_code += "      },\n";
  }

  lua_code += "      fetch = function(ctx)\n";
  lua_code += "        return \"" + identity + "\"\n";  // Function returns identity
  lua_code += "      end\n";
  lua_code += "    }\n";
  lua_code += "  }\n";
  lua_code += "}\n";

  auto result{ lua.safe_script(lua_code, sol::script_pass_on_error) };
  if (!result.valid()) {
    sol::error err = result;
    throw std::runtime_error("Lua error: " + std::string{ err.what() });
  }
}

// Helper: Create and parse a recipe_spec with custom source fetch
// The fetch function returns the recipe identity for verification
envy::recipe_spec *create_recipe_with_custom_fetch(
    sol::state &lua,
    std::string const &identity,
    std::vector<std::string> const &dep_identities = {}) {
  // Set up dependencies global for lookup
  setup_recipe_environment(lua, identity, dep_identities);

  // Build Lua code that creates a recipe with custom source fetch
  std::string lua_code{ "return {\n  recipe = \"" + identity + "\",\n  source = {\n" };

  if (!dep_identities.empty()) {
    lua_code += "    dependencies = {\n";
    for (auto const &dep_id : dep_identities) {
      lua_code += "      { recipe = \"" + dep_id + "\", source = \"file:///tmp/" + dep_id +
                  ".lua\" },\n";
    }
    lua_code += "    },\n";
  }

  lua_code += "    fetch = function(ctx)\n";
  lua_code += "      return \"" + identity + "\"\n";  // Function returns identity
  lua_code += "    end\n";
  lua_code += "  }\n}";

  // Execute Lua code to create table
  auto result{ lua.safe_script(lua_code, sol::script_pass_on_error) };
  if (!result.valid()) {
    sol::error err = result;
    throw std::runtime_error("Lua error: " + std::string{ err.what() });
  }

  sol::object recipe_val{ result };
  return envy::recipe_spec::parse(recipe_val, fs::current_path());
}

// Helper: Call a recipe_spec's custom fetch function and return result
std::string call_custom_fetch(sol::state &lua, envy::recipe_spec const *spec) {
  if (!spec->has_fetch_function()) {
    throw std::runtime_error("recipe_spec has no custom fetch function");
  }

  // Look up function using dynamic lookup
  sol::table deps = lua["dependencies"];
  if (!deps.valid()) { throw std::runtime_error("dependencies global not found"); }

  // Find the matching dependency
  for (size_t i{ 1 };; ++i) {
    sol::object dep_entry{ deps[i] };
    if (!dep_entry.valid()) { break; }

    if (dep_entry.is<sol::table>()) {
      sol::table dep_table{ dep_entry.as<sol::table>() };
      sol::object recipe_obj{ dep_table["recipe"] };
      if (recipe_obj.valid() && recipe_obj.is<std::string>() &&
          recipe_obj.as<std::string>() == spec->identity) {
        sol::table source_table = dep_table["source"];
        sol::function fetch_func = source_table["fetch"];

        // Create dummy ctx table
        sol::table ctx{ lua.create_table() };

        // Call function(ctx)
        auto result{ fetch_func(ctx) };
        if (!result.valid()) { throw std::runtime_error("Function call failed"); }

        return result.get<std::string>();
      }
    }
  }

  throw std::runtime_error("Failed to lookup source.fetch for " + spec->identity);
}

}  // namespace

TEST_CASE("recipe_spec - function returns correct identity") {
  auto lua_state{ envy::sol_util_make_lua_state() };
  sol::state &lua{ *lua_state };

  envy::recipe_spec *spec{ create_recipe_with_custom_fetch(lua, "local.foo@v1") };

  CHECK(spec->identity == "local.foo@v1");
  CHECK(spec->has_fetch_function());

  // Call the function and verify it returns the identity
  std::string result{ call_custom_fetch(lua, spec) };
  CHECK(result == "local.foo@v1");
}

TEST_CASE("recipe_spec - multiple specs have correct functions") {
  auto lua_state{ envy::sol_util_make_lua_state() };
  sol::state &lua{ *lua_state };

  // Set up dependencies global with all three recipes
  std::string lua_code{ R"(
    dependencies = {
      {
        recipe = "local.foo@v1",
        source = {
          fetch = function(ctx) return "local.foo@v1" end
        }
      },
      {
        recipe = "local.bar@v1",
        source = {
          fetch = function(ctx) return "local.bar@v1" end
        }
      },
      {
        recipe = "local.baz@v1",
        source = {
          fetch = function(ctx) return "local.baz@v1" end
        }
      }
    }
  )" };

  lua.safe_script(lua_code);

  // Parse each spec (they'll all use the same dependencies global)
  sol::table deps_table = lua["dependencies"];
  sol::object val_foo{ deps_table[1] };
  sol::object val_bar{ deps_table[2] };
  sol::object val_baz{ deps_table[3] };

  envy::recipe_spec *spec_foo{ envy::recipe_spec::parse(val_foo, fs::current_path()) };
  envy::recipe_spec *spec_bar{ envy::recipe_spec::parse(val_bar, fs::current_path()) };
  envy::recipe_spec *spec_baz{ envy::recipe_spec::parse(val_baz, fs::current_path()) };

  // Verify each has a function
  CHECK(spec_foo->has_fetch_function());
  CHECK(spec_bar->has_fetch_function());
  CHECK(spec_baz->has_fetch_function());

  // Call each function and verify correct association
  CHECK(call_custom_fetch(lua, spec_foo) == "local.foo@v1");
  CHECK(call_custom_fetch(lua, spec_bar) == "local.bar@v1");
  CHECK(call_custom_fetch(lua, spec_baz) == "local.baz@v1");
}

TEST_CASE("recipe_spec - with source dependencies") {
  auto lua_state{ envy::sol_util_make_lua_state() };
  sol::state &lua{ *lua_state };

  envy::recipe_spec *spec{ create_recipe_with_custom_fetch(
      lua,
      "local.parent@v1",
      { "local.tool1@v1", "local.tool2@v1" }) };

  CHECK(spec->identity == "local.parent@v1");
  CHECK(spec->source_dependencies.size() == 2);
  CHECK(spec->source_dependencies[0]->identity == "local.tool1@v1");
  CHECK(spec->source_dependencies[1]->identity == "local.tool2@v1");

  // Function still works
  CHECK(call_custom_fetch(lua, spec) == "local.parent@v1");
}

TEST_CASE("recipe_spec - function persists across multiple calls") {
  auto lua_state{ envy::sol_util_make_lua_state() };
  sol::state &lua{ *lua_state };

  envy::recipe_spec *spec{ create_recipe_with_custom_fetch(lua, "local.persistent@v1") };

  // Call the function many times
  for (int i{ 0 }; i < 50; ++i) {
    CHECK(call_custom_fetch(lua, spec) == "local.persistent@v1");
  }
}

TEST_CASE("recipe_spec - error on dependencies without fetch") {
  auto lua_state{ envy::sol_util_make_lua_state() };
  sol::state &lua{ *lua_state };

  std::string lua_code{ R"(
    return {
      recipe = "local.broken@v1",
      source = {
        dependencies = {
          { recipe = "local.tool@v1", source = "file:///tmp/tool.lua" }
        }
      }
    }
  )" };

  CHECK_THROWS_WITH(
      [&]() {
        auto result{ lua.safe_script(lua_code, sol::script_pass_on_error) };
        sol::object val{ result };
        envy::recipe_spec::parse(val, fs::current_path());
      }(),
      "source.dependencies requires source.fetch function");
}

TEST_CASE("recipe_spec - error on fetch not a function") {
  auto lua_state{ envy::sol_util_make_lua_state() };
  sol::state &lua{ *lua_state };

  std::string lua_code{ R"(
    return {
      recipe = "local.broken@v1",
      source = {
        dependencies = {
          { recipe = "local.tool@v1", source = "file:///tmp/tool.lua" }
        },
        fetch = "not-a-function"
      }
    }
  )" };

  CHECK_THROWS_WITH(
      [&]() {
        auto result{ lua.safe_script(lua_code, sol::script_pass_on_error) };
        sol::object val{ result };
        envy::recipe_spec::parse(val, fs::current_path());
      }(),
      "source.fetch must be a function");
}

TEST_CASE("recipe_spec - error on dependencies not array") {
  auto lua_state{ envy::sol_util_make_lua_state() };
  sol::state &lua{ *lua_state };

  std::string lua_code{ R"(
    return {
      recipe = "local.broken@v1",
      source = {
        dependencies = "not-an-array",
        fetch = function(ctx) end
      }
    }
  )" };

  CHECK_THROWS_WITH(
      [&]() {
        auto result{ lua.safe_script(lua_code, sol::script_pass_on_error) };
        sol::object val{ result };
        envy::recipe_spec::parse(val, fs::current_path());
      }(),
      "source.dependencies must be array (table)");
}

TEST_CASE("recipe_spec - error on empty source table") {
  auto lua_state{ envy::sol_util_make_lua_state() };
  sol::state &lua{ *lua_state };

  std::string lua_code{ R"(
    return {
      recipe = "local.broken@v1",
      source = {}
    }
  )" };

  CHECK_THROWS_WITH(
      [&]() {
        auto result{ lua.safe_script(lua_code, sol::script_pass_on_error) };
        sol::object val{ result };
        envy::recipe_spec::parse(val, fs::current_path());
      }(),
      "source table must have either URL string or dependencies+fetch function");
}

TEST_CASE("recipe_spec - error on parse without lua_State") {
  auto lua_state{ envy::sol_util_make_lua_state() };
  sol::state &lua{ *lua_state };

  std::string lua_code{ R"(
    return {
      recipe = "local.test@v1",
      source = {
        fetch = function(ctx) end
      }
    }
  )" };

  auto result{ lua.safe_script(lua_code, sol::script_pass_on_error) };
  sol::object val{ result };

  // Verify parsing with lua_State works for custom source.fetch
  CHECK_NOTHROW(envy::recipe_spec::parse(val, fs::current_path()));
}

TEST_CASE("recipe_spec - no function without source table") {
  auto lua_state{ envy::sol_util_make_lua_state() };
  sol::state &lua{ *lua_state };

  std::string lua_code{ R"(
    return {
      recipe = "local.normal@v1",
      source = "file:///tmp/normal.lua"
    }
  )" };

  auto result{ lua.safe_script(lua_code, sol::script_pass_on_error) };
  sol::object val{ result };
  envy::recipe_spec *spec{ envy::recipe_spec::parse(val, fs::current_path()) };

  CHECK(spec->identity == "local.normal@v1");
  CHECK_FALSE(spec->has_fetch_function());
  CHECK(spec->source_dependencies.empty());
}
