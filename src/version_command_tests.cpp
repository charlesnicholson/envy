#include "version_command.h"

#include "doctest.h"
#include "oneapi/tbb/flow_graph.h"

TEST_CASE("version_command constructor accepts config") {
  envy::version_command::config cfg;
  envy::version_command cmd{ cfg };
}

TEST_CASE("version_command config has correct command_type_t typedef") {
  using config_type = envy::version_command::config;
  using expected_command = envy::version_command;
  using actual_command = config_type::command_type_t;

  CHECK(std::is_same_v<actual_command, expected_command>);
}

TEST_CASE("version_command schedule is callable") {
  envy::version_command::config cfg;
  envy::version_command cmd{ cfg };

  tbb::flow::graph g;
  cmd.schedule(g);
}
