#include "cmd_lua.h"

#include "doctest.h"
#include "oneapi/tbb/flow_graph.h"

#include <filesystem>

TEST_CASE("cmd_lua constructor accepts config") {
  envy::cmd_lua::config cfg;
  cfg.script_path = "/tmp/test.lua";
  envy::cmd_lua cmd{ cfg };
  CHECK(cmd.get_config().script_path == cfg.script_path);
}

TEST_CASE("cmd_lua config has correct command_type_t typedef") {
  using config_type = envy::cmd_lua::config;
  using expected_command = envy::cmd_lua;
  using actual_command = config_type::command_type_t;
  CHECK(std::is_same_v<actual_command, expected_command>);
}

TEST_CASE("cmd_lua schedule is callable") {
  envy::cmd_lua::config cfg;
  cfg.script_path = "/tmp/test.lua";
  envy::cmd_lua cmd{ cfg };

  tbb::flow::graph g;
  cmd.schedule(g);  // Should not throw
}
