#include "lua_shell.h"

#include "shell.h"

#include "doctest.h"

#include <cstdint>
#include <stdexcept>
#include <variant>

namespace {

// Helper to set light userdata in a table (sol2 requires lua_State* access)
void set_light_userdata(sol::table &tbl, char const *key, void *ptr) {
  lua_State *L{ tbl.lua_state() };
  lua_pushlightuserdata(L, ptr);
  sol::stack_object obj{ L, -1 };
  tbl[key] = obj;
  lua_pop(L, 1);
}

// Helper: Create Lua state with ENVY_SHELL constants registered
sol::state make_test_lua_state() {
  sol::state lua;
  lua.open_libraries(sol::lib::base);

  // Register ENVY_SHELL table with constants (cast enum to void* for light userdata)
  sol::table envy_shell{ lua.create_table() };
  set_light_userdata(envy_shell, "BASH", reinterpret_cast<void *>(static_cast<uintptr_t>(envy::shell_choice::bash)));
  set_light_userdata(envy_shell, "SH", reinterpret_cast<void *>(static_cast<uintptr_t>(envy::shell_choice::sh)));
  set_light_userdata(envy_shell, "CMD", reinterpret_cast<void *>(static_cast<uintptr_t>(envy::shell_choice::cmd)));
  set_light_userdata(envy_shell, "POWERSHELL", reinterpret_cast<void *>(static_cast<uintptr_t>(envy::shell_choice::powershell)));
  lua["ENVY_SHELL"] = envy_shell;

  return lua;
}

}  // namespace

TEST_CASE("parse_shell_config_from_lua - ENVY_SHELL.BASH") {
  sol::state lua{ make_test_lua_state() };

  sol::object bash_obj{ lua["ENVY_SHELL"]["BASH"] };
  auto result{ envy::parse_shell_config_from_lua(bash_obj, "test") };

  CHECK(std::holds_alternative<envy::shell_choice>(result));
  CHECK(std::get<envy::shell_choice>(result) == envy::shell_choice::bash);
}

TEST_CASE("parse_shell_config_from_lua - ENVY_SHELL.SH") {
  sol::state lua{ make_test_lua_state() };

  sol::object sh_obj{ lua["ENVY_SHELL"]["SH"] };
  auto result{ envy::parse_shell_config_from_lua(sh_obj, "test") };

  CHECK(std::holds_alternative<envy::shell_choice>(result));
  CHECK(std::get<envy::shell_choice>(result) == envy::shell_choice::sh);
}

TEST_CASE("parse_shell_config_from_lua - ENVY_SHELL.CMD") {
  sol::state lua{ make_test_lua_state() };

  sol::object cmd_obj{ lua["ENVY_SHELL"]["CMD"] };
  auto result{ envy::parse_shell_config_from_lua(cmd_obj, "test") };

  CHECK(std::holds_alternative<envy::shell_choice>(result));
  CHECK(std::get<envy::shell_choice>(result) == envy::shell_choice::cmd);
}

TEST_CASE("parse_shell_config_from_lua - ENVY_SHELL.POWERSHELL") {
  sol::state lua{ make_test_lua_state() };

  sol::object powershell_obj{ lua["ENVY_SHELL"]["POWERSHELL"] };
  auto result{ envy::parse_shell_config_from_lua(powershell_obj, "test") };

  CHECK(std::holds_alternative<envy::shell_choice>(result));
  CHECK(std::get<envy::shell_choice>(result) == envy::shell_choice::powershell);
}

TEST_CASE("parse_shell_config_from_lua - invalid ENVY_SHELL constant") {
  sol::state lua{ make_test_lua_state() };

  // Create invalid light userdata (value 999, not a valid shell_choice)
  lua_State *L{ lua.lua_state() };
  lua_pushlightuserdata(L, reinterpret_cast<void *>(static_cast<uintptr_t>(999)));
  sol::object invalid_obj{ sol::stack_object{ L, -1 } };
  lua_pop(L, 1);

  CHECK_THROWS_WITH_AS(envy::parse_shell_config_from_lua(invalid_obj, "test_ctx"),
                       "test_ctx: invalid ENVY_SHELL constant",
                       std::runtime_error);
}

TEST_CASE("parse_shell_config_from_lua - custom shell file-based") {
  sol::state lua{ make_test_lua_state() };

#ifdef _WIN32
  // Windows: use PowerShell
  sol::table shell_tbl{ lua.create_table() };
  shell_tbl["file"] = "C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe";
  shell_tbl["ext"] = ".ps1";

  sol::object shell_obj{ shell_tbl };
  auto result{ envy::parse_shell_config_from_lua(shell_obj, "test") };

  CHECK(std::holds_alternative<envy::custom_shell_file>(result));
  auto const &shell_file{ std::get<envy::custom_shell_file>(result) };
  REQUIRE(shell_file.argv.size() == 1);
  CHECK(shell_file.argv[0] == "C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe");
  CHECK(shell_file.ext == ".ps1");
#else
  // Unix: use sh
  sol::table shell_tbl{ lua.create_table() };
  shell_tbl["file"] = "/bin/sh";
  shell_tbl["ext"] = ".sh";

  sol::object shell_obj{ shell_tbl };
  auto result{ envy::parse_shell_config_from_lua(shell_obj, "test") };

  CHECK(std::holds_alternative<envy::custom_shell_file>(result));
  auto const &shell_file{ std::get<envy::custom_shell_file>(result) };
  REQUIRE(shell_file.argv.size() == 1);
  CHECK(shell_file.argv[0] == "/bin/sh");
  CHECK(shell_file.ext == ".sh");
#endif
}

TEST_CASE("parse_shell_config_from_lua - custom shell inline") {
  sol::state lua{ make_test_lua_state() };

#ifdef _WIN32
  // Windows: use cmd.exe
  sol::table inline_arr{ lua.create_table() };
  inline_arr[1] = "C:\\Windows\\System32\\cmd.exe";
  inline_arr[2] = "/c";

  sol::table shell_tbl{ lua.create_table() };
  shell_tbl["inline"] = inline_arr;

  sol::object shell_obj{ shell_tbl };
  auto result{ envy::parse_shell_config_from_lua(shell_obj, "test") };

  CHECK(std::holds_alternative<envy::custom_shell_inline>(result));
  auto const &shell_inline{ std::get<envy::custom_shell_inline>(result) };
  REQUIRE(shell_inline.argv.size() == 2);
  CHECK(shell_inline.argv[0] == "C:\\Windows\\System32\\cmd.exe");
  CHECK(shell_inline.argv[1] == "/c");
#else
  // Unix: use sh
  sol::table inline_arr{ lua.create_table() };
  inline_arr[1] = "/bin/sh";
  inline_arr[2] = "-c";

  sol::table shell_tbl{ lua.create_table() };
  shell_tbl["inline"] = inline_arr;

  sol::object shell_obj{ shell_tbl };
  auto result{ envy::parse_shell_config_from_lua(shell_obj, "test") };

  CHECK(std::holds_alternative<envy::custom_shell_inline>(result));
  auto const &shell_inline{ std::get<envy::custom_shell_inline>(result) };
  REQUIRE(shell_inline.argv.size() == 2);
  CHECK(shell_inline.argv[0] == "/bin/sh");
  CHECK(shell_inline.argv[1] == "-c");
#endif
}

TEST_CASE("parse_shell_config_from_lua - custom shell missing fields") {
  sol::state lua{ make_test_lua_state() };

  // Create table with only 'file', missing 'ext'
  sol::table shell_tbl{ lua.create_table() };
  shell_tbl["file"] = "/bin/zsh";

  sol::object shell_obj{ shell_tbl };
  CHECK_THROWS_WITH_AS(
      envy::parse_shell_config_from_lua(shell_obj, "test_ctx"),
      "test_ctx: file mode requires 'ext' field (e.g., \".sh\", \".tcl\")",
      std::runtime_error);
}

TEST_CASE("parse_shell_config_from_lua - custom shell wrong type for inline") {
  sol::state lua{ make_test_lua_state() };

  // Create table with 'inline' as string instead of array
  sol::table shell_tbl{ lua.create_table() };
  shell_tbl["inline"] = "/bin/sh";

  sol::object shell_obj{ shell_tbl };
  CHECK_THROWS_WITH_AS(envy::parse_shell_config_from_lua(shell_obj, "test_ctx"),
                       "test_ctx: 'inline' key must be an array of strings",
                       std::runtime_error);
}

TEST_CASE("parse_shell_config_from_lua - custom shell both inline and file") {
  sol::state lua{ make_test_lua_state() };

  // Create table with both 'inline' and 'file'
  sol::table inline_arr{ lua.create_table() };
  inline_arr[1] = "/bin/sh";

  sol::table shell_tbl{ lua.create_table() };
  shell_tbl["inline"] = inline_arr;
  shell_tbl["file"] = "/bin/bash";
  shell_tbl["ext"] = ".sh";

  sol::object shell_obj{ shell_tbl };
  CHECK_THROWS_WITH_AS(
      envy::parse_shell_config_from_lua(shell_obj, "test_ctx"),
      "test_ctx: custom shell table cannot have both 'file' and 'inline' keys",
      std::runtime_error);
}

TEST_CASE("parse_shell_config_from_lua - custom shell empty table") {
  sol::state lua{ make_test_lua_state() };

  // Create empty table
  sol::table shell_tbl{ lua.create_table() };

  sol::object shell_obj{ shell_tbl };
  CHECK_THROWS_WITH_AS(
      envy::parse_shell_config_from_lua(shell_obj, "test_ctx"),
      "test_ctx: custom shell table must have either 'file' or 'inline' key",
      std::runtime_error);
}

TEST_CASE("parse_shell_config_from_lua - string type not supported") {
  sol::state lua{ make_test_lua_state() };

  sol::object str_obj{ sol::make_object(lua, "bash") };

  CHECK_THROWS_WITH_AS(envy::parse_shell_config_from_lua(str_obj, "test_ctx"),
                       "test_ctx: shell must be ENVY_SHELL constant or table {file=..., "
                       "ext=...} or {inline=...}",
                       std::runtime_error);
}

TEST_CASE("parse_shell_config_from_lua - number type not supported") {
  sol::state lua{ make_test_lua_state() };

  sol::object num_obj{ sol::make_object(lua, 42) };

  CHECK_THROWS_WITH_AS(envy::parse_shell_config_from_lua(num_obj, "test_ctx"),
                       "test_ctx: shell must be ENVY_SHELL constant or table {file=..., "
                       "ext=...} or {inline=...}",
                       std::runtime_error);
}

TEST_CASE("parse_shell_config_from_lua - nil type not supported") {
  sol::state lua{ make_test_lua_state() };

  sol::object nil_obj{ sol::make_object(lua, sol::lua_nil) };

  CHECK_THROWS_WITH_AS(envy::parse_shell_config_from_lua(nil_obj, "test_ctx"),
                       "test_ctx: shell must be ENVY_SHELL constant or table {file=..., "
                       "ext=...} or {inline=...}",
                       std::runtime_error);
}

TEST_CASE("parse_shell_config_from_lua - boolean type not supported") {
  sol::state lua{ make_test_lua_state() };

  sol::object bool_obj{ sol::make_object(lua, true) };

  CHECK_THROWS_WITH_AS(envy::parse_shell_config_from_lua(bool_obj, "test_ctx"),
                       "test_ctx: shell must be ENVY_SHELL constant or table {file=..., "
                       "ext=...} or {inline=...}",
                       std::runtime_error);
}

TEST_CASE("parse_shell_config_from_lua - error context in message") {
  sol::state lua{ make_test_lua_state() };

  sol::object invalid_obj{ sol::make_object(lua, "invalid") };

  // Test with different context strings
  CHECK_THROWS_WITH_AS(envy::parse_shell_config_from_lua(invalid_obj, "ctx.run"),
                       "ctx.run: shell must be ENVY_SHELL constant or table {file=..., "
                       "ext=...} or {inline=...}",
                       std::runtime_error);

  CHECK_THROWS_WITH_AS(envy::parse_shell_config_from_lua(invalid_obj, "default_shell"),
                       "default_shell: shell must be ENVY_SHELL constant or table "
                       "{file=..., ext=...} or {inline=...}",
                       std::runtime_error);

  CHECK_THROWS_WITH_AS(envy::parse_shell_config_from_lua(invalid_obj, "default_shell function"),
                       "default_shell function: shell must be ENVY_SHELL constant or "
                       "table {file=..., ext=...} or {inline=...}",
                       std::runtime_error);
}
