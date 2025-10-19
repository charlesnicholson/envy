#include "cmd.h"
#include "cmd_lua.h"

#include "doctest.h"

TEST_CASE("cmd factory creates cmd_lua from config") {
  envy::cmd_lua::config cfg;
  cfg.script_path = "/tmp/test.lua";

  auto command_ptr{ envy::cmd::create(cfg) };

  REQUIRE(command_ptr != nullptr);
  CHECK(dynamic_cast<envy::cmd_lua *>(command_ptr.get()) != nullptr);
}

TEST_CASE("cmd_cfg provides correct command_type_t typedef") {
  using config_type = envy::cmd_lua::config;
  using expected_command = envy::cmd_lua;
  using actual_command = config_type::command_type_t;

  CHECK(std::is_same_v<actual_command, expected_command>);
}
