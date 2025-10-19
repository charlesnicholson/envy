#include "lua_command.h"

#include "doctest.h"
#include "oneapi/tbb/flow_graph.h"

#include <filesystem>

TEST_CASE("lua_command constructor accepts config") {
  envy::lua_command::config cfg;
  cfg.script_path = "/tmp/test.lua";
  envy::lua_command cmd{ cfg };
}

TEST_CASE("lua_command config has correct command_type_t typedef") {
  using config_type = envy::lua_command::config;
  using expected_command = envy::lua_command;
  using actual_command = config_type::command_type_t;
  CHECK(std::is_same_v<actual_command, expected_command>);
}

TEST_CASE("lua_command schedule is callable") {
  envy::lua_command::config cfg;
  cfg.script_path = "/tmp/test.lua";
  envy::lua_command cmd{ cfg };

  tbb::flow::graph g;
  cmd.schedule(g);  // Should not throw
}
