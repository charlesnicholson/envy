#include "cli.h"
#include "cmd_lua.h"
#include "cmd_playground.h"
#include "cmd_version.h"

#include "doctest.h"

#include <filesystem>
#include <fstream>
#include <vector>

namespace {

// Helper to convert vector of strings to argc/argv
std::vector<char*> make_argv(std::vector<std::string>& args) {
  std::vector<char*> argv;
  for (auto& arg : args) {
    argv.push_back(arg.data());
  }
  argv.push_back(nullptr);
  return argv;
}

}  // anonymous namespace

TEST_CASE("cli_parse: no arguments") {
  std::vector<std::string> args{"envy"};
  auto argv{make_argv(args)};

  auto cmd{envy::cli_parse(static_cast<int>(args.size()), argv.data())};

  CHECK(cmd == nullptr);
}

TEST_CASE("cli_parse: cmd_version") {
  SUBCASE("-v flag") {
    std::vector<std::string> args{"envy", "-v"};
    auto argv{make_argv(args)};

    auto cmd{envy::cli_parse(static_cast<int>(args.size()), argv.data())};

    REQUIRE(cmd != nullptr);
    CHECK(dynamic_cast<envy::cmd_version*>(cmd.get()) != nullptr);
  }

  SUBCASE("--version flag") {
    std::vector<std::string> args{"envy", "--version"};
    auto argv{make_argv(args)};

    auto cmd{envy::cli_parse(static_cast<int>(args.size()), argv.data())};

    REQUIRE(cmd != nullptr);
    CHECK(dynamic_cast<envy::cmd_version*>(cmd.get()) != nullptr);
  }
}

TEST_CASE("cli_parse: cmd_lua") {
  SUBCASE("with script path") {
    // Create temporary test file
    auto temp_path{std::filesystem::temp_directory_path() / "cli_test_script.lua"};
    {
      std::ofstream temp_file{temp_path};
      temp_file << "-- test script\n";
    }

    std::vector<std::string> args{"envy", "lua", temp_path.string()};
    auto argv{make_argv(args)};

    auto cmd{envy::cli_parse(static_cast<int>(args.size()), argv.data())};

    // Clean up temp file
    std::filesystem::remove(temp_path);

    REQUIRE(cmd != nullptr);
    auto* lua_cmd{dynamic_cast<envy::cmd_lua*>(cmd.get())};
    REQUIRE(lua_cmd != nullptr);
    CHECK(lua_cmd->get_config().script_path == temp_path);
  }
}

TEST_CASE("cli_parse: cmd_playground") {
  SUBCASE("s3_uri only") {
    std::vector<std::string> args{"envy", "playground", "s3://bucket/key"};
    auto argv{make_argv(args)};

    auto cmd{envy::cli_parse(static_cast<int>(args.size()), argv.data())};

    REQUIRE(cmd != nullptr);
    auto* playground_cmd{dynamic_cast<envy::cmd_playground*>(cmd.get())};
    REQUIRE(playground_cmd != nullptr);
    CHECK(playground_cmd->get_config().s3_uri == "s3://bucket/key");
    CHECK(playground_cmd->get_config().region.empty());
  }

  SUBCASE("s3_uri with region") {
    std::vector<std::string> args{"envy", "playground", "s3://bucket/key", "us-west-2"};
    auto argv{make_argv(args)};

    auto cmd{envy::cli_parse(static_cast<int>(args.size()), argv.data())};

    REQUIRE(cmd != nullptr);
    auto* playground_cmd{dynamic_cast<envy::cmd_playground*>(cmd.get())};
    REQUIRE(playground_cmd != nullptr);
    CHECK(playground_cmd->get_config().s3_uri == "s3://bucket/key");
    CHECK(playground_cmd->get_config().region == "us-west-2");
  }
}
