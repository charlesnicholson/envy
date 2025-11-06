#include "cli.h"
#include "commands/cmd_extract.h"
#include "commands/cmd_fetch.h"
#include "commands/cmd_lua.h"
#include "commands/cmd_playground.h"
#include "commands/cmd_version.h"

#include "doctest.h"

#include <filesystem>
#include <fstream>
#include <variant>
#include <vector>

namespace {

// Helper to convert vector of strings to argc/argv
std::vector<char *> make_argv(std::vector<std::string> &args) {
  std::vector<char *> argv;
  for (auto &arg : args) { argv.push_back(arg.data()); }
  argv.push_back(nullptr);
  return argv;
}

}  // anonymous namespace

TEST_CASE("cli_parse: no arguments") {
  std::vector<std::string> args{ "envy" };
  auto argv{ make_argv(args) };

  auto parsed{ envy::cli_parse(static_cast<int>(args.size()), argv.data()) };

  // With no arguments, help text returned and no command configuration.
  CHECK_FALSE(parsed.cmd_cfg.has_value());
}

TEST_CASE("cli_parse: cmd_version") {
  SUBCASE("-v flag") {
    std::vector<std::string> args{ "envy", "-v" };
    auto argv{ make_argv(args) };

    auto parsed{ envy::cli_parse(static_cast<int>(args.size()), argv.data()) };

    REQUIRE(parsed.cmd_cfg.has_value());
    CHECK(std::holds_alternative<envy::cmd_version::cfg>(*parsed.cmd_cfg));
  }

  SUBCASE("--version flag") {
    std::vector<std::string> args{ "envy", "--version" };
    auto argv{ make_argv(args) };

    auto parsed{ envy::cli_parse(static_cast<int>(args.size()), argv.data()) };

    REQUIRE(parsed.cmd_cfg.has_value());
    CHECK(std::holds_alternative<envy::cmd_version::cfg>(*parsed.cmd_cfg));
  }
}

TEST_CASE("cli_parse: cmd_extract") {
  SUBCASE("archive and destination") {
    // Create temporary test archive
    auto temp_archive{ std::filesystem::temp_directory_path() /
                       "cli_test_archive.tar.gz" };
    auto temp_dest{ std::filesystem::temp_directory_path() / "cli_test_dest" };
    {
      std::ofstream temp_file{ temp_archive };
      temp_file << "fake archive\n";
    }

    std::vector<std::string> args{ "envy",
                                   "extract",
                                   temp_archive.string(),
                                   temp_dest.string() };
    auto argv{ make_argv(args) };

    auto parsed{ envy::cli_parse(static_cast<int>(args.size()), argv.data()) };

    // Clean up temp file
    std::filesystem::remove(temp_archive);

    REQUIRE(parsed.cmd_cfg.has_value());
    auto const *cfg{ std::get_if<envy::cmd_extract::cfg>(&*parsed.cmd_cfg) };
    REQUIRE(cfg != nullptr);
    CHECK(cfg->archive_path == temp_archive);
    CHECK(cfg->destination == temp_dest);
  }

  SUBCASE("archive without destination") {
    // Create temporary test archive
    auto temp_archive{ std::filesystem::temp_directory_path() /
                       "cli_test_archive2.tar.gz" };
    {
      std::ofstream temp_file{ temp_archive };
      temp_file << "fake archive\n";
    }

    std::vector<std::string> args{ "envy", "extract", temp_archive.string() };
    auto argv{ make_argv(args) };

    auto parsed{ envy::cli_parse(static_cast<int>(args.size()), argv.data()) };

    // Clean up temp file
    std::filesystem::remove(temp_archive);

    REQUIRE(parsed.cmd_cfg.has_value());
    auto const *cfg{ std::get_if<envy::cmd_extract::cfg>(&*parsed.cmd_cfg) };
    REQUIRE(cfg != nullptr);
    CHECK(cfg->archive_path == temp_archive);
    CHECK(cfg->destination.empty());
  }
}

TEST_CASE("cli_parse: cmd_fetch") {
  SUBCASE("basic fetch command") {
    std::vector<std::string> args{ "envy",
                                   "fetch",
                                   "https://example.com/archive.tar.gz",
                                   "/tmp/local.tar.gz" };
    auto argv{ make_argv(args) };

    auto parsed{ envy::cli_parse(static_cast<int>(args.size()), argv.data()) };

    REQUIRE(parsed.cmd_cfg.has_value());
    auto const *cfg{ std::get_if<envy::cmd_fetch::cfg>(&*parsed.cmd_cfg) };
    REQUIRE(cfg != nullptr);
    CHECK(cfg->source == "https://example.com/archive.tar.gz");
    CHECK(cfg->destination == std::filesystem::path("/tmp/local.tar.gz"));
    CHECK_FALSE(cfg->manifest_root.has_value());
  }

  SUBCASE("fetch with manifest root") {
    std::vector<std::string> args{ "envy",
                                   "fetch",
                                   "file://relative/path/tool.tar.gz",
                                   "/tmp/tool.tar.gz",
                                   "--manifest-root",
                                   "/workspace" };
    auto argv{ make_argv(args) };

    auto parsed{ envy::cli_parse(static_cast<int>(args.size()), argv.data()) };

    REQUIRE(parsed.cmd_cfg.has_value());
    auto const *cfg{ std::get_if<envy::cmd_fetch::cfg>(&*parsed.cmd_cfg) };
    REQUIRE(cfg != nullptr);
    CHECK(cfg->source == "file://relative/path/tool.tar.gz");
    CHECK(cfg->destination == std::filesystem::path("/tmp/tool.tar.gz"));
    REQUIRE(cfg->manifest_root.has_value());
    CHECK(*cfg->manifest_root == std::filesystem::path("/workspace"));
  }
}

TEST_CASE("cli_parse: cmd_lua") {
  SUBCASE("with script path") {
    // Create temporary test file
    auto temp_path{ std::filesystem::temp_directory_path() / "cli_test_script.lua" };
    {
      std::ofstream temp_file{ temp_path };
      temp_file << "-- test script\n";
    }

    std::vector<std::string> args{ "envy", "lua", temp_path.string() };
    auto argv{ make_argv(args) };

    auto parsed{ envy::cli_parse(static_cast<int>(args.size()), argv.data()) };

    // Clean up temp file
    std::filesystem::remove(temp_path);

    REQUIRE(parsed.cmd_cfg.has_value());
    auto const *cfg{ std::get_if<envy::cmd_lua::cfg>(&*parsed.cmd_cfg) };
    REQUIRE(cfg != nullptr);
    CHECK(cfg->script_path == temp_path);
  }
}

TEST_CASE("cli_parse: cmd_playground") {
  SUBCASE("uri only") {
    std::vector<std::string> args{ "envy", "playground", "s3://bucket/key" };
    auto argv{ make_argv(args) };

    auto parsed{ envy::cli_parse(static_cast<int>(args.size()), argv.data()) };

    REQUIRE(parsed.cmd_cfg.has_value());
    auto const *cfg{ std::get_if<envy::cmd_playground::cfg>(&*parsed.cmd_cfg) };
    REQUIRE(cfg != nullptr);
    CHECK(cfg->uri == "s3://bucket/key");
    CHECK(cfg->region.empty());
    REQUIRE(parsed.verbosity.has_value());
    CHECK(parsed.verbosity == envy::tui::level::TUI_INFO);
    CHECK_FALSE(parsed.structured_logging);
  }

  SUBCASE("uri with region") {
    std::vector<std::string> args{ "envy", "playground", "s3://bucket/key", "us-west-2" };
    auto argv{ make_argv(args) };

    auto parsed{ envy::cli_parse(static_cast<int>(args.size()), argv.data()) };

    REQUIRE(parsed.cmd_cfg.has_value());
    auto const *cfg{ std::get_if<envy::cmd_playground::cfg>(&*parsed.cmd_cfg) };
    REQUIRE(cfg != nullptr);
    CHECK(cfg->uri == "s3://bucket/key");
    CHECK(cfg->region == "us-west-2");
    REQUIRE(parsed.verbosity.has_value());
    CHECK(parsed.verbosity == envy::tui::level::TUI_INFO);
    CHECK_FALSE(parsed.structured_logging);
  }
}

TEST_CASE("cli_parse: verbose flag") {
  std::vector<std::string> args{ "envy", "--verbose", "playground", "s3://bucket/key" };
  auto argv{ make_argv(args) };

  auto parsed{ envy::cli_parse(static_cast<int>(args.size()), argv.data()) };

  REQUIRE(parsed.cmd_cfg.has_value());
  REQUIRE(parsed.verbosity.has_value());
  CHECK(parsed.verbosity == envy::tui::level::TUI_DEBUG);
  CHECK(parsed.structured_logging);
}
