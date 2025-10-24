#include "cmd_lua.h"
#include "tui.h"

#include "doctest.h"
#include "oneapi/tbb/flow_graph.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

TEST_CASE("cmd_lua constructor accepts config") {
  envy::cmd_lua::cfg cfg;
  cfg.script_path = "/tmp/test.lua";
  envy::cmd_lua cmd{ cfg };
  CHECK(cmd.get_cfg().script_path == cfg.script_path);
}

TEST_CASE("cmd_lua config exposes cmd_t alias") {
  using config_type = envy::cmd_lua::cfg;
  using expected_command = envy::cmd_lua;
  using actual_command = config_type::cmd_t;
  CHECK(std::is_same_v<actual_command, expected_command>);
}

TEST_CASE("cmd_lua schedule is callable") {
  envy::cmd_lua::cfg cfg;
  cfg.script_path = "/tmp/test.lua";
  envy::cmd_lua cmd{ cfg };

  tbb::flow::graph g;
  cmd.schedule(g);  // Should not throw
}

namespace {

struct lua_test_fixture {
  std::vector<std::string> messages;

  lua_test_fixture() {
    envy::tui::set_output_handler(
        [this](std::string_view value) { messages.emplace_back(value); });
  }

  ~lua_test_fixture() {
    envy::tui::set_output_handler([](std::string_view) {});
  }

  void run_script(std::filesystem::path const &script_path) {
    envy::tui::run(std::nullopt);
    envy::cmd_lua::cfg cfg;
    cfg.script_path = script_path;
    envy::cmd_lua cmd{ cfg };
    tbb::flow::graph g;
    cmd.schedule(g);
    g.wait_for_all();
    envy::tui::shutdown();
  }

  std::vector<std::string> filter_output() const {
    std::vector<std::string> script_output;
    for (auto const &msg : messages) {
      if (msg.find("Failed") == std::string::npos && msg.find("error") == std::string::npos) {
        script_output.push_back(msg);
      }
    }
    return script_output;
  }
};

}  // namespace

TEST_CASE_FIXTURE(lua_test_fixture, "lua print with single arg") {
  run_script("test_data/lua/print_single.lua");
  auto script_output{ filter_output() };
  REQUIRE(script_output.size() >= 1);
  CHECK(script_output[0] == "hello\n");
}

TEST_CASE_FIXTURE(lua_test_fixture, "lua print with multiple args uses tabs") {
  run_script("test_data/lua/print_multiple.lua");
  auto script_output{ filter_output() };
  REQUIRE(script_output.size() >= 1);
  CHECK(script_output[0] == "a\tb\tc\n");
}

TEST_CASE_FIXTURE(lua_test_fixture, "lua print with mixed types uses tabs") {
  run_script("test_data/lua/print_mixed_types.lua");
  auto script_output{ filter_output() };
  REQUIRE(script_output.size() >= 1);
  CHECK(script_output[0] == "value\t42\ttrue\tnil\n");
}

TEST_CASE_FIXTURE(lua_test_fixture, "envy.info outputs to tui") {
  run_script("test_data/lua/envy_info.lua");
  auto script_output{ filter_output() };
  REQUIRE(script_output.size() >= 1);
  CHECK(script_output[0] == "test message\n");
}

TEST_CASE_FIXTURE(lua_test_fixture, "envy.warn outputs to tui") {
  run_script("test_data/lua/envy_warn.lua");

  std::vector<std::string> script_output;
  for (auto const &msg : messages) {
    if (msg.find("Failed") == std::string::npos) {
      script_output.push_back(msg);
    }
  }

  REQUIRE(script_output.size() >= 1);
  CHECK(script_output[0] == "warning message\n");
}
