#include "cmds/cmd_version.h"

#include "doctest.h"

TEST_CASE("cmd_version config exposes cmd_t alias") {
  using config_type = envy::cmd_version::cfg;
  using expected_command = envy::cmd_version;
  using actual_command = config_type::cmd_t;

  CHECK(std::is_same_v<actual_command, expected_command>);
}
