#include "cli.h"

#include "lua_command.h"
#include "playground_command.h"
#include "version_command.h"

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

TEST_CASE("cli_parse: version_command") {
  SUBCASE("-v flag") {
    std::vector<std::string> args{"envy", "-v"};
    auto argv{make_argv(args)};

    auto cmd{envy::cli_parse(static_cast<int>(args.size()), argv.data())};

    REQUIRE(cmd != nullptr);
    CHECK(dynamic_cast<envy::version_command*>(cmd.get()) != nullptr);
  }

  SUBCASE("--version flag") {
    std::vector<std::string> args{"envy", "--version"};
    auto argv{make_argv(args)};

    auto cmd{envy::cli_parse(static_cast<int>(args.size()), argv.data())};

    REQUIRE(cmd != nullptr);
    CHECK(dynamic_cast<envy::version_command*>(cmd.get()) != nullptr);
  }
}

TEST_CASE("cli_parse: lua_command") {
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
    auto* lua_cmd{dynamic_cast<envy::lua_command*>(cmd.get())};
    CHECK(lua_cmd != nullptr);
  }
}

TEST_CASE("cli_parse: playground_command") {
  SUBCASE("s3_uri only") {
    std::vector<std::string> args{"envy", "playground", "s3://bucket/key"};
    auto argv{make_argv(args)};

    auto cmd{envy::cli_parse(static_cast<int>(args.size()), argv.data())};

    REQUIRE(cmd != nullptr);
    auto* playground_cmd{dynamic_cast<envy::playground_command*>(cmd.get())};
    CHECK(playground_cmd != nullptr);
  }

  SUBCASE("s3_uri with region") {
    std::vector<std::string> args{"envy", "playground", "s3://bucket/key", "us-west-2"};
    auto argv{make_argv(args)};

    auto cmd{envy::cli_parse(static_cast<int>(args.size()), argv.data())};

    REQUIRE(cmd != nullptr);
    auto* playground_cmd{dynamic_cast<envy::playground_command*>(cmd.get())};
    CHECK(playground_cmd != nullptr);
  }
}
