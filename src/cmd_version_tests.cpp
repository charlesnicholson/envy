#include "cmd_version.h"

#include "doctest.h"
#include "oneapi/tbb/flow_graph.h"

TEST_CASE("cmd_version constructor accepts config") {
  envy::cmd_version::cfg cfg;
  envy::cmd_version cmd{ cfg };
  CHECK_NOTHROW(cmd.get_cfg());
}

TEST_CASE("cmd_version config exposes cmd_t alias") {
  using config_type = envy::cmd_version::cfg;
  using expected_command = envy::cmd_version;
  using actual_command = config_type::cmd_t;

  CHECK(std::is_same_v<actual_command, expected_command>);
}

TEST_CASE("cmd_version schedule is callable") {
  envy::cmd_version::cfg cfg;
  envy::cmd_version cmd{ cfg };

  tbb::flow::graph g;
  cmd.schedule(g);
}
