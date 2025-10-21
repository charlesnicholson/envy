#include "cli.h"
#include "cmd_lua.h"
#include "cmd_playground.h"
#include "cmd_version.h"
#include "tui.h"

#include "CLI11.hpp"

#include <iostream>
#include <optional>

namespace envy {

std::optional<cli_args> cli_parse(int argc, char **argv) {
  CLI::App app{ "envy - freeform package manager" };

  bool show_version{ false };
  app.add_flag("-v,--version", show_version, "Show version information");

  bool verbose{ false };
  app.add_flag("--verbose", verbose, "Enable structured verbose logging");

  std::optional<cli_args::cmd_cfg_t> cmd_cfg;

  // Lua subcommand
  cmd_lua::cfg lua_cfg{};
  auto *lua{ app.add_subcommand("lua", "Execute Lua script") };
  lua->add_option("script", lua_cfg.script_path, "Lua script file to execute")
      ->required()
      ->check(CLI::ExistingFile);
  lua->callback([&cmd_cfg, &lua_cfg] { cmd_cfg = lua_cfg; });

  // Playground subcommand
  cmd_playground::cfg playground_cfg{};
  auto *playground{ app.add_subcommand("playground", "Run S3/Git/Curl playground demo") };
  playground->add_option("s3_uri", playground_cfg.s3_uri, "S3 URI (s3://bucket/key)")
      ->required();
  playground->add_option("region", playground_cfg.region, "AWS region (optional)");
  playground->callback([&cmd_cfg, &playground_cfg] { cmd_cfg = playground_cfg; });

  try {
    app.parse(argc, argv);
  } catch (CLI::CallForHelp const &) {
    std::cout << app.help() << '\n';
    return std::nullopt;
  } catch (CLI::ParseError const &e) {
    std::cerr << e.what() << '\n';
    return std::nullopt;
  }

  cli_args args{};
  if (show_version) {
    args.cmd_cfg = cmd_version::cfg{};
  } else {
    if (!cmd_cfg) {
      std::cerr << app.help() << "\n";
      return std::nullopt;
    }
    args.cmd_cfg = *cmd_cfg;
  }

  if (verbose) { args.verbosity = tui::level::TUI_DEBUG; }

  return args;
}

}  // namespace envy
