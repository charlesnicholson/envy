#include "cli.h"
#include "tui.h"

#include "CLI11.hpp"

#include <optional>

namespace envy {

cli_args cli_parse(int argc, char **argv) {
  CLI::App app{ "envy - freeform package manager" };

  bool verbose{ false };
  app.add_flag("-v,--verbose", verbose, "Enable structured verbose logging");

  std::optional<cli_args::cmd_cfg_t> cmd_cfg;

  // Version subcommand
  auto *version{ app.add_subcommand("version", "Show version information") };
  version->callback([&cmd_cfg] { cmd_cfg = cmd_version::cfg{}; });

  // Extract subcommand
  cmd_extract::cfg extract_cfg{};
  auto *extract{ app.add_subcommand("extract", "Extract archive to destination") };
  extract->add_option("archive", extract_cfg.archive_path, "Archive file to extract")
      ->required()
      ->check(CLI::ExistingFile);
  extract->add_option("destination",
                      extract_cfg.destination,
                      "Destination directory (defaults to current directory)");
  extract->callback([&cmd_cfg, &extract_cfg] { cmd_cfg = extract_cfg; });

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

  cli_args args{};

  auto const apply_verbosity{ [&args, &verbose] {
    if (verbose) { args.verbosity = tui::level::TUI_DEBUG; }
  } };

  try {
    app.parse(argc, argv);
  } catch (CLI::CallForHelp const &) {
    args.cli_output = app.help();
    apply_verbosity();
    return args;
  } catch (CLI::ParseError const &e) {
    args.cli_output = std::string(e.what());
    apply_verbosity();
    return args;
  }

  if (!cmd_cfg) {
    args.cli_output = app.help();
    apply_verbosity();
    return args;
  }

  args.cmd_cfg = *cmd_cfg;
  apply_verbosity();

  return args;
}

}  // namespace envy
