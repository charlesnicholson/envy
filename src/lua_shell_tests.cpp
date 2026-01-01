#include "lua_shell.h"

#include "shell.h"
#include "sol_util.h"

#include "doctest.h"

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <variant>

namespace {

using envy::sol_state_ptr;

// Helper to set light userdata in a table using Sol helpers
void set_light_userdata(sol::table &tbl, char const *key, void *ptr) {
  tbl[key] = sol::lightuserdata_value{ ptr };
}

// Helper: Create Lua state with ENVY_SHELL constants registered
// All constants are registered on all platforms; runtime validation rejects incompatible
// shells
sol_state_ptr make_test_lua_state() {
  auto lua{ envy::sol_util_make_lua_state() };

  // Register ENVY_SHELL table with all constants on all platforms
  sol::table envy_shell{ lua->create_table() };
  set_light_userdata(
      envy_shell,
      "BASH",
      reinterpret_cast<void *>(static_cast<uintptr_t>(envy::shell_choice::bash)));
  set_light_userdata(
      envy_shell,
      "SH",
      reinterpret_cast<void *>(static_cast<uintptr_t>(envy::shell_choice::sh)));
  set_light_userdata(
      envy_shell,
      "CMD",
      reinterpret_cast<void *>(static_cast<uintptr_t>(envy::shell_choice::cmd)));
  set_light_userdata(
      envy_shell,
      "POWERSHELL",
      reinterpret_cast<void *>(static_cast<uintptr_t>(envy::shell_choice::powershell)));
  (*lua)["ENVY_SHELL"] = envy_shell;

  return lua;
}

}  // namespace

#if !defined(_WIN32)
TEST_CASE("parse_shell_config_from_lua - ENVY_SHELL.BASH on Unix") {
  auto lua{ make_test_lua_state() };

  sol::object bash_obj{ sol::make_object(*lua,
                                         static_cast<int>(envy::shell_choice::bash)) };
  auto result{ envy::parse_shell_config_from_lua(bash_obj, "test") };

  CHECK(std::holds_alternative<envy::shell_choice>(result));
  CHECK(std::get<envy::shell_choice>(result) == envy::shell_choice::bash);
}

TEST_CASE("parse_shell_config_from_lua - ENVY_SHELL.SH on Unix") {
  auto lua{ make_test_lua_state() };

  sol::object sh_obj{ sol::make_object(*lua, static_cast<int>(envy::shell_choice::sh)) };
  auto result{ envy::parse_shell_config_from_lua(sh_obj, "test") };

  CHECK(std::holds_alternative<envy::shell_choice>(result));
  CHECK(std::get<envy::shell_choice>(result) == envy::shell_choice::sh);
}
#endif

#if defined(_WIN32)
TEST_CASE("parse_shell_config_from_lua - ENVY_SHELL.CMD on Windows") {
  auto lua{ make_test_lua_state() };

  sol::object cmd_obj{ sol::make_object(*lua, static_cast<int>(envy::shell_choice::cmd)) };
  auto result{ envy::parse_shell_config_from_lua(cmd_obj, "test") };

  CHECK(std::holds_alternative<envy::shell_choice>(result));
  CHECK(std::get<envy::shell_choice>(result) == envy::shell_choice::cmd);
}

TEST_CASE("parse_shell_config_from_lua - ENVY_SHELL.POWERSHELL on Windows") {
  auto lua{ make_test_lua_state() };

  sol::object powershell_obj{
    sol::make_object(*lua, static_cast<int>(envy::shell_choice::powershell))
  };
  auto result{ envy::parse_shell_config_from_lua(powershell_obj, "test") };

  CHECK(std::holds_alternative<envy::shell_choice>(result));
  CHECK(std::get<envy::shell_choice>(result) == envy::shell_choice::powershell);
}

TEST_CASE("parse_shell_config_from_lua - ENVY_SHELL.BASH rejected on Windows") {
  auto lua{ make_test_lua_state() };

  sol::object bash_obj{ sol::make_object(*lua,
                                         static_cast<int>(envy::shell_choice::bash)) };

  CHECK_THROWS_WITH_AS(envy::parse_shell_config_from_lua(bash_obj, "test_ctx"),
                       "test_ctx: BASH/SH shells are only available on Unix",
                       std::runtime_error);
}

TEST_CASE("parse_shell_config_from_lua - ENVY_SHELL.SH rejected on Windows") {
  auto lua{ make_test_lua_state() };

  sol::object sh_obj{ sol::make_object(*lua, static_cast<int>(envy::shell_choice::sh)) };

  CHECK_THROWS_WITH_AS(envy::parse_shell_config_from_lua(sh_obj, "test_ctx"),
                       "test_ctx: BASH/SH shells are only available on Unix",
                       std::runtime_error);
}
#else
TEST_CASE("parse_shell_config_from_lua - ENVY_SHELL.CMD rejected on Unix") {
  auto lua{ make_test_lua_state() };

  sol::object cmd_obj{ sol::make_object(*lua, static_cast<int>(envy::shell_choice::cmd)) };

  CHECK_THROWS_WITH_AS(envy::parse_shell_config_from_lua(cmd_obj, "test_ctx"),
                       "test_ctx: CMD/POWERSHELL shells are only available on Windows",
                       std::runtime_error);
}

TEST_CASE("parse_shell_config_from_lua - ENVY_SHELL.POWERSHELL rejected on Unix") {
  auto lua{ make_test_lua_state() };

  sol::object powershell_obj{
    sol::make_object(*lua, static_cast<int>(envy::shell_choice::powershell))
  };

  CHECK_THROWS_WITH_AS(envy::parse_shell_config_from_lua(powershell_obj, "test_ctx"),
                       "test_ctx: CMD/POWERSHELL shells are only available on Windows",
                       std::runtime_error);
}
#endif

TEST_CASE("parse_shell_config_from_lua - invalid ENVY_SHELL constant") {
  auto lua{ make_test_lua_state() };

  // Create invalid constant (value 999, not a valid shell_choice)
  sol::object invalid_obj{ *lua, sol::in_place, 999 };

  CHECK_THROWS_WITH_AS(envy::parse_shell_config_from_lua(invalid_obj, "test_ctx"),
                       "test_ctx: invalid ENVY_SHELL constant",
                       std::runtime_error);
}

TEST_CASE("parse_shell_config_from_lua - custom shell file-based") {
  auto lua{ make_test_lua_state() };

#ifdef _WIN32
  // Windows: use PowerShell
  sol::table shell_tbl{ lua->create_table() };
  shell_tbl["file"] = "C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe";
  shell_tbl["ext"] = ".ps1";

  sol::object shell_obj = shell_tbl;
  auto result{ envy::parse_shell_config_from_lua(shell_obj, "test") };

  CHECK(std::holds_alternative<envy::custom_shell_file>(result));
  auto const &shell_file{ std::get<envy::custom_shell_file>(result) };
  REQUIRE(shell_file.argv.size() == 1);
  CHECK(shell_file.argv[0] ==
        "C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe");
  CHECK(shell_file.ext == ".ps1");
#else
  // Unix: use sh
  sol::table shell_tbl{ lua->create_table() };
  shell_tbl["file"] = "/bin/sh";
  shell_tbl["ext"] = ".sh";

  sol::object shell_obj = shell_tbl;
  auto result{ envy::parse_shell_config_from_lua(shell_obj, "test") };

  CHECK(std::holds_alternative<envy::custom_shell_file>(result));
  auto const &shell_file{ std::get<envy::custom_shell_file>(result) };
  REQUIRE(shell_file.argv.size() == 1);
  CHECK(shell_file.argv[0] == "/bin/sh");
  CHECK(shell_file.ext == ".sh");
#endif
}

TEST_CASE("parse_shell_config_from_lua - custom shell inline") {
  auto lua{ make_test_lua_state() };

#ifdef _WIN32
  // Windows: use cmd.exe
  sol::table inline_arr{ lua->create_table() };
  inline_arr[1] = "C:\\Windows\\System32\\cmd.exe";
  inline_arr[2] = "/c";

  sol::table shell_tbl{ lua->create_table() };
  shell_tbl["inline"] = inline_arr;

  sol::object shell_obj = shell_tbl;
  auto result{ envy::parse_shell_config_from_lua(shell_obj, "test") };

  CHECK(std::holds_alternative<envy::custom_shell_inline>(result));
  auto const &shell_inline{ std::get<envy::custom_shell_inline>(result) };
  REQUIRE(shell_inline.argv.size() == 2);
  CHECK(shell_inline.argv[0] == "C:\\Windows\\System32\\cmd.exe");
  CHECK(shell_inline.argv[1] == "/c");
#else
  // Unix: use sh
  sol::table inline_arr{ lua->create_table() };
  inline_arr[1] = "/bin/sh";
  inline_arr[2] = "-c";

  sol::table shell_tbl{ lua->create_table() };
  shell_tbl["inline"] = inline_arr;

  sol::object shell_obj = shell_tbl;
  auto result{ envy::parse_shell_config_from_lua(shell_obj, "test") };

  CHECK(std::holds_alternative<envy::custom_shell_inline>(result));
  auto const &shell_inline{ std::get<envy::custom_shell_inline>(result) };
  REQUIRE(shell_inline.argv.size() == 2);
  CHECK(shell_inline.argv[0] == "/bin/sh");
  CHECK(shell_inline.argv[1] == "-c");
#endif
}

TEST_CASE("parse_shell_config_from_lua - custom shell missing fields") {
  auto lua{ make_test_lua_state() };

  // Create table with only 'file', missing 'ext'
  sol::table shell_tbl{ lua->create_table() };
  shell_tbl["file"] = "/bin/zsh";

  sol::object shell_obj = shell_tbl;
  CHECK_THROWS_WITH_AS(
      envy::parse_shell_config_from_lua(shell_obj, "test_ctx"),
      "test_ctx: file mode requires 'ext' field (e.g., \".sh\", \".tcl\")",
      std::runtime_error);
}

TEST_CASE("parse_shell_config_from_lua - custom shell wrong type for inline") {
  auto lua{ make_test_lua_state() };

  // Create table with 'inline' as string instead of array
  sol::table shell_tbl{ lua->create_table() };
  shell_tbl["inline"] = "/bin/sh";

  sol::object shell_obj = shell_tbl;
  CHECK_THROWS_WITH_AS(envy::parse_shell_config_from_lua(shell_obj, "test_ctx"),
                       "test_ctx: 'inline' key must be an array of strings",
                       std::runtime_error);
}

TEST_CASE("parse_shell_config_from_lua - custom shell both inline and file") {
  auto lua{ make_test_lua_state() };

  // Create table with both 'inline' and 'file'
  sol::table inline_arr{ lua->create_table() };
  inline_arr[1] = "/bin/sh";

  sol::table shell_tbl{ lua->create_table() };
  shell_tbl["inline"] = inline_arr;
  shell_tbl["file"] = "/bin/bash";
  shell_tbl["ext"] = ".sh";

  sol::object shell_obj = shell_tbl;
  CHECK_THROWS_WITH_AS(
      envy::parse_shell_config_from_lua(shell_obj, "test_ctx"),
      "test_ctx: custom shell table cannot have both 'file' and 'inline' keys",
      std::runtime_error);
}

TEST_CASE("parse_shell_config_from_lua - custom shell empty table") {
  auto lua{ make_test_lua_state() };

  // Create empty table
  sol::table shell_tbl{ lua->create_table() };

  sol::object shell_obj = shell_tbl;
  CHECK_THROWS_WITH_AS(
      envy::parse_shell_config_from_lua(shell_obj, "test_ctx"),
      "test_ctx: custom shell table must have either 'file' or 'inline' key",
      std::runtime_error);
}

TEST_CASE("parse_shell_config_from_lua - string type not supported") {
  auto lua{ make_test_lua_state() };

  sol::object str_obj{ sol::make_object(*lua, "bash") };

  CHECK_THROWS_WITH_AS(envy::parse_shell_config_from_lua(str_obj, "test_ctx"),
                       "test_ctx: shell must be ENVY_SHELL constant or table {file=..., "
                       "ext=...} or {inline=...}",
                       std::runtime_error);
}

TEST_CASE("parse_shell_config_from_lua - invalid numeric constant") {
  auto lua{ make_test_lua_state() };

  sol::object num_obj{ sol::make_object(*lua, 42) };

  CHECK_THROWS_WITH_AS(envy::parse_shell_config_from_lua(num_obj, "test_ctx"),
                       "test_ctx: invalid ENVY_SHELL constant",
                       std::runtime_error);
}

TEST_CASE("parse_shell_config_from_lua - nil type not supported") {
  auto lua{ make_test_lua_state() };

  sol::object nil_obj{ sol::make_object(*lua, sol::lua_nil) };

  CHECK_THROWS_WITH_AS(envy::parse_shell_config_from_lua(nil_obj, "test_ctx"),
                       "test_ctx: shell must be ENVY_SHELL constant or table {file=..., "
                       "ext=...} or {inline=...}",
                       std::runtime_error);
}

TEST_CASE("parse_shell_config_from_lua - boolean type not supported") {
  auto lua{ make_test_lua_state() };

  sol::object bool_obj{ sol::make_object(*lua, true) };

  CHECK_THROWS_WITH_AS(envy::parse_shell_config_from_lua(bool_obj, "test_ctx"),
                       "test_ctx: shell must be ENVY_SHELL constant or table {file=..., "
                       "ext=...} or {inline=...}",
                       std::runtime_error);
}

TEST_CASE("parse_shell_config_from_lua - error context in message") {
  auto lua{ make_test_lua_state() };

  sol::object invalid_obj{ sol::make_object(*lua, "invalid") };

  // Test with different context strings
  CHECK_THROWS_WITH_AS(envy::parse_shell_config_from_lua(invalid_obj, "ctx.run"),
                       "ctx.run: shell must be ENVY_SHELL constant or table {file=..., "
                       "ext=...} or {inline=...}",
                       std::runtime_error);

  CHECK_THROWS_WITH_AS(envy::parse_shell_config_from_lua(invalid_obj, "DEFAULT_SHELL"),
                       "DEFAULT_SHELL: shell must be ENVY_SHELL constant or table "
                       "{file=..., ext=...} or {inline=...}",
                       std::runtime_error);

  CHECK_THROWS_WITH_AS(
      envy::parse_shell_config_from_lua(invalid_obj, "DEFAULT_SHELL function"),
      "DEFAULT_SHELL function: shell must be ENVY_SHELL constant or "
      "table {file=..., ext=...} or {inline=...}",
      std::runtime_error);
}
