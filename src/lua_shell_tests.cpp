#include "lua_shell.h"

#include "shell.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

#include "doctest.h"

#include <cstdint>
#include <stdexcept>
#include <variant>

namespace {

// Helper: Create Lua state with ENVY_SHELL constants registered
lua_State *make_test_lua_state() {
  lua_State *L{ luaL_newstate() };
  luaL_openlibs(L);

  // Register ENVY_SHELL table with constants
  lua_createtable(L, 0, 4);

  lua_pushlightuserdata(
      L,
      reinterpret_cast<void *>(static_cast<uintptr_t>(envy::shell_choice::bash)));
  lua_setfield(L, -2, "BASH");

  lua_pushlightuserdata(
      L,
      reinterpret_cast<void *>(static_cast<uintptr_t>(envy::shell_choice::sh)));
  lua_setfield(L, -2, "SH");

  lua_pushlightuserdata(
      L,
      reinterpret_cast<void *>(static_cast<uintptr_t>(envy::shell_choice::cmd)));
  lua_setfield(L, -2, "CMD");

  lua_pushlightuserdata(
      L,
      reinterpret_cast<void *>(static_cast<uintptr_t>(envy::shell_choice::powershell)));
  lua_setfield(L, -2, "POWERSHELL");

  lua_setglobal(L, "ENVY_SHELL");

  return L;
}

}  // namespace

TEST_CASE("parse_shell_config_from_lua - ENVY_SHELL.BASH") {
  lua_State *L{ make_test_lua_state() };

  lua_getglobal(L, "ENVY_SHELL");
  lua_getfield(L, -1, "BASH");

  auto result{ envy::parse_shell_config_from_lua(L, -1, "test") };

  CHECK(std::holds_alternative<envy::shell_choice>(result));
  CHECK(std::get<envy::shell_choice>(result) == envy::shell_choice::bash);

  lua_close(L);
}

TEST_CASE("parse_shell_config_from_lua - ENVY_SHELL.SH") {
  lua_State *L{ make_test_lua_state() };

  lua_getglobal(L, "ENVY_SHELL");
  lua_getfield(L, -1, "SH");

  auto result{ envy::parse_shell_config_from_lua(L, -1, "test") };

  CHECK(std::holds_alternative<envy::shell_choice>(result));
  CHECK(std::get<envy::shell_choice>(result) == envy::shell_choice::sh);

  lua_close(L);
}

TEST_CASE("parse_shell_config_from_lua - ENVY_SHELL.CMD") {
  lua_State *L{ make_test_lua_state() };

  lua_getglobal(L, "ENVY_SHELL");
  lua_getfield(L, -1, "CMD");

  auto result{ envy::parse_shell_config_from_lua(L, -1, "test") };

  CHECK(std::holds_alternative<envy::shell_choice>(result));
  CHECK(std::get<envy::shell_choice>(result) == envy::shell_choice::cmd);

  lua_close(L);
}

TEST_CASE("parse_shell_config_from_lua - ENVY_SHELL.POWERSHELL") {
  lua_State *L{ make_test_lua_state() };

  lua_getglobal(L, "ENVY_SHELL");
  lua_getfield(L, -1, "POWERSHELL");

  auto result{ envy::parse_shell_config_from_lua(L, -1, "test") };

  CHECK(std::holds_alternative<envy::shell_choice>(result));
  CHECK(std::get<envy::shell_choice>(result) == envy::shell_choice::powershell);

  lua_close(L);
}

TEST_CASE("parse_shell_config_from_lua - invalid ENVY_SHELL constant") {
  lua_State *L{ make_test_lua_state() };

  // Push invalid light userdata (value 999, not a valid shell_choice)
  lua_pushlightuserdata(L, reinterpret_cast<void *>(static_cast<uintptr_t>(999)));

  CHECK_THROWS_WITH_AS(envy::parse_shell_config_from_lua(L, -1, "test_ctx"),
                       "test_ctx: invalid ENVY_SHELL constant",
                       std::runtime_error);

  lua_close(L);
}

TEST_CASE("parse_shell_config_from_lua - custom shell file-based") {
  lua_State *L{ make_test_lua_state() };

  // Create table: { file = "/bin/zsh", ext = ".zsh" }
  lua_createtable(L, 0, 2);
  lua_pushstring(L, "/bin/zsh");
  lua_setfield(L, -2, "file");
  lua_pushstring(L, ".zsh");
  lua_setfield(L, -2, "ext");

  auto result{ envy::parse_shell_config_from_lua(L, -1, "test") };

  CHECK(std::holds_alternative<envy::custom_shell_file>(result));
  auto const &shell_file{ std::get<envy::custom_shell_file>(result) };
  REQUIRE(shell_file.argv.size() == 1);
  CHECK(shell_file.argv[0] == "/bin/zsh");
  CHECK(shell_file.ext == ".zsh");

  lua_close(L);
}

TEST_CASE("parse_shell_config_from_lua - custom shell inline") {
  lua_State *L{ make_test_lua_state() };

  // Create table: { inline = {"/bin/sh", "-c"} }
  lua_createtable(L, 0, 1);
  lua_createtable(L, 2, 0);  // Create array for inline
  lua_pushstring(L, "/bin/sh");
  lua_rawseti(L, -2, 1);
  lua_pushstring(L, "-c");
  lua_rawseti(L, -2, 2);
  lua_setfield(L, -2, "inline");

  auto result{ envy::parse_shell_config_from_lua(L, -1, "test") };

  CHECK(std::holds_alternative<envy::custom_shell_inline>(result));
  auto const &shell_inline{ std::get<envy::custom_shell_inline>(result) };
  REQUIRE(shell_inline.argv.size() == 2);
  CHECK(shell_inline.argv[0] == "/bin/sh");
  CHECK(shell_inline.argv[1] == "-c");

  lua_close(L);
}

TEST_CASE("parse_shell_config_from_lua - custom shell missing fields") {
  lua_State *L{ make_test_lua_state() };

  // Create table with only 'file', missing 'ext'
  lua_createtable(L, 0, 1);
  lua_pushstring(L, "/bin/zsh");
  lua_setfield(L, -2, "file");

  CHECK_THROWS_WITH_AS(
      envy::parse_shell_config_from_lua(L, -1, "test_ctx"),
      "test_ctx: file mode requires 'ext' field (e.g., \".sh\", \".tcl\")",
      std::runtime_error);

  lua_close(L);
}

TEST_CASE("parse_shell_config_from_lua - custom shell wrong type for inline") {
  lua_State *L{ make_test_lua_state() };

  // Create table with 'inline' as string instead of array
  lua_createtable(L, 0, 1);
  lua_pushstring(L, "/bin/sh");
  lua_setfield(L, -2, "inline");

  CHECK_THROWS_WITH_AS(envy::parse_shell_config_from_lua(L, -1, "test_ctx"),
                       "test_ctx: 'inline' key must be an array of strings",
                       std::runtime_error);

  lua_close(L);
}

TEST_CASE("parse_shell_config_from_lua - custom shell both inline and file") {
  lua_State *L{ make_test_lua_state() };

  // Create table with both 'inline' and 'file'
  lua_createtable(L, 0, 3);
  // inline must be an array
  lua_createtable(L, 1, 0);
  lua_pushstring(L, "/bin/sh");
  lua_rawseti(L, -2, 1);
  lua_setfield(L, -2, "inline");
  lua_pushstring(L, "/bin/bash");
  lua_setfield(L, -2, "file");
  lua_pushstring(L, ".sh");
  lua_setfield(L, -2, "ext");

  CHECK_THROWS_WITH_AS(
      envy::parse_shell_config_from_lua(L, -1, "test_ctx"),
      "test_ctx: custom shell table cannot have both 'file' and 'inline' keys",
      std::runtime_error);

  lua_close(L);
}

TEST_CASE("parse_shell_config_from_lua - custom shell empty table") {
  lua_State *L{ make_test_lua_state() };

  // Create empty table
  lua_createtable(L, 0, 0);

  CHECK_THROWS_WITH_AS(
      envy::parse_shell_config_from_lua(L, -1, "test_ctx"),
      "test_ctx: custom shell table must have either 'file' or 'inline' key",
      std::runtime_error);

  lua_close(L);
}

TEST_CASE("parse_shell_config_from_lua - string type not supported") {
  lua_State *L{ make_test_lua_state() };

  lua_pushstring(L, "bash");

  CHECK_THROWS_WITH_AS(envy::parse_shell_config_from_lua(L, -1, "test_ctx"),
                       "test_ctx: shell must be ENVY_SHELL constant or table {file=..., "
                       "ext=...} or {inline=...}",
                       std::runtime_error);

  lua_close(L);
}

TEST_CASE("parse_shell_config_from_lua - number type not supported") {
  lua_State *L{ make_test_lua_state() };

  lua_pushnumber(L, 42);

  CHECK_THROWS_WITH_AS(envy::parse_shell_config_from_lua(L, -1, "test_ctx"),
                       "test_ctx: shell must be ENVY_SHELL constant or table {file=..., "
                       "ext=...} or {inline=...}",
                       std::runtime_error);

  lua_close(L);
}

TEST_CASE("parse_shell_config_from_lua - nil type not supported") {
  lua_State *L{ make_test_lua_state() };

  lua_pushnil(L);

  CHECK_THROWS_WITH_AS(envy::parse_shell_config_from_lua(L, -1, "test_ctx"),
                       "test_ctx: shell must be ENVY_SHELL constant or table {file=..., "
                       "ext=...} or {inline=...}",
                       std::runtime_error);

  lua_close(L);
}

TEST_CASE("parse_shell_config_from_lua - boolean type not supported") {
  lua_State *L{ make_test_lua_state() };

  lua_pushboolean(L, 1);

  CHECK_THROWS_WITH_AS(envy::parse_shell_config_from_lua(L, -1, "test_ctx"),
                       "test_ctx: shell must be ENVY_SHELL constant or table {file=..., "
                       "ext=...} or {inline=...}",
                       std::runtime_error);

  lua_close(L);
}

TEST_CASE("parse_shell_config_from_lua - error context in message") {
  lua_State *L{ make_test_lua_state() };

  lua_pushstring(L, "invalid");

  // Test with different context strings
  CHECK_THROWS_WITH_AS(envy::parse_shell_config_from_lua(L, -1, "ctx.run"),
                       "ctx.run: shell must be ENVY_SHELL constant or table {file=..., "
                       "ext=...} or {inline=...}",
                       std::runtime_error);

  CHECK_THROWS_WITH_AS(envy::parse_shell_config_from_lua(L, -1, "default_shell"),
                       "default_shell: shell must be ENVY_SHELL constant or table "
                       "{file=..., ext=...} or {inline=...}",
                       std::runtime_error);

  CHECK_THROWS_WITH_AS(envy::parse_shell_config_from_lua(L, -1, "default_shell function"),
                       "default_shell function: shell must be ENVY_SHELL constant or "
                       "table {file=..., ext=...} or {inline=...}",
                       std::runtime_error);

  lua_close(L);
}

TEST_CASE("parse_shell_config_from_lua - negative stack index") {
  lua_State *L{ make_test_lua_state() };

  lua_getglobal(L, "ENVY_SHELL");
  lua_getfield(L, -1, "BASH");
  lua_pushstring(L, "dummy");  // Push extra value to test negative indexing

  // Index -2 should be the BASH constant
  auto result{ envy::parse_shell_config_from_lua(L, -2, "test") };

  CHECK(std::holds_alternative<envy::shell_choice>(result));
  CHECK(std::get<envy::shell_choice>(result) == envy::shell_choice::bash);

  lua_close(L);
}

TEST_CASE("parse_shell_config_from_lua - positive stack index") {
  lua_State *L{ make_test_lua_state() };

  lua_getglobal(L, "ENVY_SHELL");
  lua_getfield(L, -1, "SH");
  // Stack: ENVY_SHELL table at index 1, SH constant at index 2

  auto result{ envy::parse_shell_config_from_lua(L, 2, "test") };

  CHECK(std::holds_alternative<envy::shell_choice>(result));
  CHECK(std::get<envy::shell_choice>(result) == envy::shell_choice::sh);

  lua_close(L);
}

TEST_CASE("parse_shell_config_from_lua - does not pop stack") {
  lua_State *L{ make_test_lua_state() };

  lua_getglobal(L, "ENVY_SHELL");
  lua_getfield(L, -1, "CMD");

  int const stack_before{ lua_gettop(L) };

  envy::parse_shell_config_from_lua(L, -1, "test");

  int const stack_after{ lua_gettop(L) };

  // Function should NOT pop the value
  CHECK(stack_after == stack_before);

  lua_close(L);
}
