#include "cli.h"
#include "cmd_lua.h"
#include "cmd_playground.h"
#include "cmd_version.h"

#include "doctest.h"

#include <filesystem>
#include <fstream>
#include <variant>
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

  auto parsed{envy::cli_parse(static_cast<int>(args.size()), argv.data())};

  CHECK_FALSE(parsed.cmd_cfg.has_value());
  CHECK_FALSE(parsed.cli_output.empty());
}

TEST_CASE("cli_parse: cmd_version") {
  SUBCASE("-v flag") {
    std::vector<std::string> args{"envy", "-v"};
    auto argv{make_argv(args)};

    auto parsed{envy::cli_parse(static_cast<int>(args.size()), argv.data())};

    REQUIRE(parsed.cmd_cfg.has_value());
    CHECK(std::holds_alternative<envy::cmd_version::cfg>(*parsed.cmd_cfg));
  }

  SUBCASE("--version flag") {
    std::vector<std::string> args{"envy", "--version"};
    auto argv{make_argv(args)};

    auto parsed{envy::cli_parse(static_cast<int>(args.size()), argv.data())};

    REQUIRE(parsed.cmd_cfg.has_value());
    CHECK(std::holds_alternative<envy::cmd_version::cfg>(*parsed.cmd_cfg));
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

    auto parsed{envy::cli_parse(static_cast<int>(args.size()), argv.data())};

    // Clean up temp file
    std::filesystem::remove(temp_path);

    REQUIRE(parsed.cmd_cfg.has_value());
    auto const* cfg{std::get_if<envy::cmd_lua::cfg>(&*parsed.cmd_cfg)};
    REQUIRE(cfg != nullptr);
    CHECK(cfg->script_path == temp_path);
  }
}

TEST_CASE("cli_parse: cmd_playground") {
  SUBCASE("s3_uri only") {
    std::vector<std::string> args{"envy", "playground", "s3://bucket/key"};
    auto argv{make_argv(args)};

    auto parsed{envy::cli_parse(static_cast<int>(args.size()), argv.data())};

    REQUIRE(parsed.cmd_cfg.has_value());
    auto const* cfg{std::get_if<envy::cmd_playground::cfg>(&*parsed.cmd_cfg)};
    REQUIRE(cfg != nullptr);
    CHECK(cfg->s3_uri == "s3://bucket/key");
    CHECK(cfg->region.empty());
  }

  SUBCASE("s3_uri with region") {
    std::vector<std::string> args{"envy", "playground", "s3://bucket/key", "us-west-2"};
    auto argv{make_argv(args)};

    auto parsed{envy::cli_parse(static_cast<int>(args.size()), argv.data())};

    REQUIRE(parsed.cmd_cfg.has_value());
    auto const* cfg{std::get_if<envy::cmd_playground::cfg>(&*parsed.cmd_cfg)};
    REQUIRE(cfg != nullptr);
    CHECK(cfg->s3_uri == "s3://bucket/key");
    CHECK(cfg->region == "us-west-2");
  }
}

TEST_CASE("cli_parse: verbose flag") {
  std::vector<std::string> args{"envy", "--verbose", "playground", "s3://bucket/key"};
  auto argv{make_argv(args)};

  auto parsed{envy::cli_parse(static_cast<int>(args.size()), argv.data())};

  REQUIRE(parsed.cmd_cfg.has_value());
  CHECK(parsed.verbosity == envy::tui::level::TUI_DEBUG);
}
