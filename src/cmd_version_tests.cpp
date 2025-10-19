#include "cmd_version.h"

#include "doctest.h"
#include "oneapi/tbb/flow_graph.h"

TEST_CASE("cmd_version constructor accepts config") {
  envy::cmd_version::config cfg;
  envy::cmd_version cmd{ cfg };
  CHECK_NOTHROW(cmd.get_config());
}

TEST_CASE("cmd_version config has correct command_type_t typedef") {
  using config_type = envy::cmd_version::config;
  using expected_command = envy::cmd_version;
  using actual_command = config_type::command_type_t;

  CHECK(std::is_same_v<actual_command, expected_command>);
}

TEST_CASE("cmd_version schedule is callable") {
  envy::cmd_version::config cfg;
  envy::cmd_version cmd{ cfg };

  tbb::flow::graph g;
  cmd.schedule(g);
}
