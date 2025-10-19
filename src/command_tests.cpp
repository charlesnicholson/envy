#include "command.h"
#include "lua_command.h"

#include "doctest.h"

TEST_CASE("command factory creates lua_command from config") {
  envy::lua_command::config cfg;
  cfg.script_path = "/tmp/test.lua";

  auto cmd{ envy::command::create(cfg) };

  REQUIRE(cmd != nullptr);
  CHECK(dynamic_cast<envy::lua_command *>(cmd.get()) != nullptr);
}

TEST_CASE("command_cfg provides correct command_type_t typedef") {
  using config_type = envy::lua_command::config;
  using expected_command = envy::lua_command;
  using actual_command = config_type::command_type_t;

  CHECK(std::is_same_v<actual_command, expected_command>);
}
