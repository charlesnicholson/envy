#include "cmd.h"

#include "doctest.h"

namespace {

class test_cmd : public envy::cmd {
 public:
  struct cfg : envy::cmd_cfg<test_cmd> {};
  explicit test_cmd(cfg) {}
  bool execute() override { return true; }
};

}  // namespace

TEST_CASE("cmd_cfg exposes cmd_t alias") {
  using config_type = test_cmd::cfg;
  using expected_command = test_cmd;
  using actual_command = config_type::cmd_t;
  CHECK(std::is_same_v<actual_command, expected_command>);
}

TEST_CASE("cmd factory creates command from cfg") {
  test_cmd::cfg cfg{};
  auto c{ envy::cmd::create(cfg) };
  REQUIRE(c);
  CHECK(dynamic_cast<test_cmd *>(c.get()));
}
