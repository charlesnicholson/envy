#include "cmd.h"

#include "cache.h"

#include "doctest.h"

namespace {

class test_cmd : public envy::cmd {
 public:
  struct cfg : envy::cmd_cfg<test_cmd> {};
  test_cmd(cfg, envy::cache & /*c*/) {}
  void execute() override {}
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
  envy::cache c{ std::filesystem::temp_directory_path() / "envy-test-cmd" };
  auto cmd{ envy::cmd::create(cfg, c) };
  REQUIRE(cmd);
  CHECK(dynamic_cast<test_cmd *>(cmd.get()));
}
