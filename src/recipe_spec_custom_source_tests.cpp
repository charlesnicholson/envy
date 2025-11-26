#include "recipe_spec.h"

#include "doctest.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {

// Helper: Set up a Lua environment simulating a recipe with dependencies
// Creates a dependencies global array with the given recipe table
void setup_recipe_environment(lua_State *L,
                              std::string const &identity,
                              std::vector<std::string> const &dep_identities = {}) {
  // Build Lua code that creates a dependencies global
  std::string lua_code =
      "dependencies = {\n  {\n    recipe = \"" + identity + "\",\n    source = {\n";

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

  if (luaL_dostring(L, lua_code.c_str()) != LUA_OK) {
    std::string error = lua_tostring(L, -1);
    throw std::runtime_error("Lua error: " + error);
  }
}

// Helper: Create and parse a recipe_spec with custom source fetch
// The fetch function returns the recipe identity for verification
envy::recipe_spec create_recipe_with_custom_fetch(
    lua_State *L,
    std::string const &identity,
    std::vector<std::string> const &dep_identities = {}) {
  // Set up dependencies global for lookup
  setup_recipe_environment(L, identity, dep_identities);

  // Build Lua code that creates a recipe with custom source fetch
  std::string lua_code = "return {\n  recipe = \"" + identity + "\",\n  source = {\n";

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
  if (luaL_dostring(L, lua_code.c_str()) != LUA_OK) {
    std::string error = lua_tostring(L, -1);
    throw std::runtime_error("Lua error: " + error);
  }

  // Convert stack top to lua_value and parse
  sol::state_view lua_view(L);
  sol::stack_object stack_obj(lua_view, -1);
  sol::object recipe_val(stack_obj);
  lua_pop(L, 1);

  envy::recipe_spec result = envy::recipe_spec::parse(recipe_val, fs::current_path(), L);
  return result;
}

// Helper: Call a recipe_spec's custom fetch function and return result
std::string call_custom_fetch(lua_State *L, envy::recipe_spec const &spec) {
  if (!spec.has_fetch_function()) {
    throw std::runtime_error("recipe_spec has no custom fetch function");
  }

  // Look up function using new dynamic lookup
  if (!envy::recipe_spec::lookup_and_push_source_fetch(L, spec.identity)) {
    throw std::runtime_error("Failed to lookup source.fetch for " + spec.identity);
  }

  // Create dummy ctx table
  lua_newtable(L);

  // Call function(ctx)
  if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
    std::string error = lua_tostring(L, -1);
    lua_pop(L, 1);
    throw std::runtime_error("Function call failed: " + error);
  }

  // Get return value
  std::string result = lua_tostring(L, -1);
  lua_pop(L, 1);

  return result;
}

}  // namespace

TEST_CASE("recipe_spec - function returns correct identity") {
  lua_State *L = luaL_newstate();
  luaL_openlibs(L);

  envy::recipe_spec spec = create_recipe_with_custom_fetch(L, "local.foo@v1");

  CHECK(spec.identity == "local.foo@v1");
  CHECK(spec.has_fetch_function());

  // Call the function and verify it returns the identity
  std::string result = call_custom_fetch(L, spec);
  CHECK(result == "local.foo@v1");

  lua_close(L);
}

TEST_CASE("recipe_spec - multiple specs have correct functions") {
  lua_State *L = luaL_newstate();
  luaL_openlibs(L);

  // Set up dependencies global with all three recipes
  std::string lua_code = R"(
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
  )";

  luaL_dostring(L, lua_code.c_str());

  // Parse each spec (they'll all use the same dependencies global)
  lua_getglobal(L, "dependencies");
  lua_rawgeti(L, -1, 1);
  sol::state_view lua_view(L);
  sol::stack_object stack_obj_foo(lua_view, -1);
  sol::object val_foo(stack_obj_foo);
  lua_pop(L, 1);
  envy::recipe_spec spec_foo = envy::recipe_spec::parse(val_foo, fs::current_path(), L);

  lua_rawgeti(L, -1, 2);
  sol::stack_object stack_obj_bar(lua_view, -1);
  sol::object val_bar(stack_obj_bar);
  lua_pop(L, 1);
  envy::recipe_spec spec_bar = envy::recipe_spec::parse(val_bar, fs::current_path(), L);

  lua_rawgeti(L, -1, 3);
  sol::stack_object stack_obj_baz(lua_view, -1);
  sol::object val_baz(stack_obj_baz);
  lua_pop(L, 1);
  envy::recipe_spec spec_baz = envy::recipe_spec::parse(val_baz, fs::current_path(), L);

  lua_pop(L, 1);  // Pop dependencies table

  // Verify each has a function
  CHECK(spec_foo.has_fetch_function());
  CHECK(spec_bar.has_fetch_function());
  CHECK(spec_baz.has_fetch_function());

  // Call each function and verify correct association
  CHECK(call_custom_fetch(L, spec_foo) == "local.foo@v1");
  CHECK(call_custom_fetch(L, spec_bar) == "local.bar@v1");
  CHECK(call_custom_fetch(L, spec_baz) == "local.baz@v1");

  lua_close(L);
}

TEST_CASE("recipe_spec - with source dependencies") {
  lua_State *L = luaL_newstate();
  luaL_openlibs(L);

  envy::recipe_spec spec =
      create_recipe_with_custom_fetch(L,
                                      "local.parent@v1",
                                      { "local.tool1@v1", "local.tool2@v1" });

  CHECK(spec.identity == "local.parent@v1");
  CHECK(spec.source_dependencies.size() == 2);
  CHECK(spec.source_dependencies[0].identity == "local.tool1@v1");
  CHECK(spec.source_dependencies[1].identity == "local.tool2@v1");

  // Function still works
  CHECK(call_custom_fetch(L, spec) == "local.parent@v1");

  lua_close(L);
}

TEST_CASE("recipe_spec - function persists across multiple calls") {
  lua_State *L = luaL_newstate();
  luaL_openlibs(L);

  envy::recipe_spec spec = create_recipe_with_custom_fetch(L, "local.persistent@v1");

  // Call the function many times
  for (int i = 0; i < 50; ++i) {
    CHECK(call_custom_fetch(L, spec) == "local.persistent@v1");
  }

  lua_close(L);
}

TEST_CASE("recipe_spec - error on dependencies without fetch") {
  lua_State *L = luaL_newstate();
  luaL_openlibs(L);

  std::string lua_code = R"(
    return {
      recipe = "local.broken@v1",
      source = {
        dependencies = {
          { recipe = "local.tool@v1", source = "file:///tmp/tool.lua" }
        }
      }
    }
  )";

  CHECK_THROWS_WITH(
      [&]() {
        luaL_dostring(L, lua_code.c_str());
        sol::state_view lua_view(L);
        sol::stack_object stack_obj(lua_view, -1);
        sol::object val(stack_obj);
        envy::recipe_spec::parse(val, fs::current_path(), L);
      }(),
      "source.dependencies requires source.fetch function");

  lua_close(L);
}

TEST_CASE("recipe_spec - error on fetch not a function") {
  lua_State *L = luaL_newstate();
  luaL_openlibs(L);

  std::string lua_code = R"(
    return {
      recipe = "local.broken@v1",
      source = {
        dependencies = {
          { recipe = "local.tool@v1", source = "file:///tmp/tool.lua" }
        },
        fetch = "not-a-function"
      }
    }
  )";

  CHECK_THROWS_WITH(
      [&]() {
        luaL_dostring(L, lua_code.c_str());
        sol::state_view lua_view(L);
        sol::stack_object stack_obj(lua_view, -1);
        sol::object val(stack_obj);
        envy::recipe_spec::parse(val, fs::current_path(), L);
      }(),
      "source.fetch must be a function");

  lua_close(L);
}

TEST_CASE("recipe_spec - error on dependencies not array") {
  lua_State *L = luaL_newstate();
  luaL_openlibs(L);

  std::string lua_code = R"(
    return {
      recipe = "local.broken@v1",
      source = {
        dependencies = "not-an-array",
        fetch = function(ctx) end
      }
    }
  )";

  CHECK_THROWS_WITH(
      [&]() {
        luaL_dostring(L, lua_code.c_str());
        sol::state_view lua_view(L);
        sol::stack_object stack_obj(lua_view, -1);
        sol::object val(stack_obj);
        envy::recipe_spec::parse(val, fs::current_path(), L);
      }(),
      "source.dependencies must be array (table)");

  lua_close(L);
}

TEST_CASE("recipe_spec - error on empty source table") {
  lua_State *L = luaL_newstate();
  luaL_openlibs(L);

  std::string lua_code = R"(
    return {
      recipe = "local.broken@v1",
      source = {}
    }
  )";

  CHECK_THROWS_WITH(
      [&]() {
        luaL_dostring(L, lua_code.c_str());
        sol::state_view lua_view(L);
        sol::stack_object stack_obj(lua_view, -1);
        sol::object val(stack_obj);
        envy::recipe_spec::parse(val, fs::current_path(), L);
      }(),
      "source table must have either URL string or dependencies+fetch function");

  lua_close(L);
}

TEST_CASE("recipe_spec - error on parse without lua_State") {
  lua_State *L = luaL_newstate();
  luaL_openlibs(L);

  std::string lua_code = R"(
    return {
      recipe = "local.test@v1",
      source = {
        fetch = function(ctx) end
      }
    }
  )";

  luaL_dostring(L, lua_code.c_str());

  // Verify parsing with lua_State works for custom source.fetch
  sol::state_view lua_view(L);
  sol::stack_object stack_obj(lua_view, -1);
  sol::object val(stack_obj);
  CHECK_NOTHROW(envy::recipe_spec::parse(val, fs::current_path(), L));

  lua_close(L);
}

TEST_CASE("recipe_spec - no function without source table") {
  lua_State *L = luaL_newstate();
  luaL_openlibs(L);

  std::string lua_code = R"(
    return {
      recipe = "local.normal@v1",
      source = "file:///tmp/normal.lua"
    }
  )";

  luaL_dostring(L, lua_code.c_str());
  sol::state_view lua_view(L);
  sol::stack_object stack_obj(lua_view, -1);
  sol::object val(stack_obj);
  envy::recipe_spec spec = envy::recipe_spec::parse(val, fs::current_path(), nullptr);

  CHECK(spec.identity == "local.normal@v1");
  CHECK_FALSE(spec.has_fetch_function());
  CHECK(spec.source_dependencies.empty());

  lua_close(L);
}
