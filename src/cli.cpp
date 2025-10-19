#include "cli.h"

#include "CLI11.hpp"
#include "lua_command.h"
#include "playground_command.h"
#include "version_command.h"

#include <iostream>
#include <variant>

namespace envy {
namespace {

using command_config =
    std::variant<lua_command::config, playground_command::config, version_command::config>;

command::ptr_t create_command(command_config const &config) {
  return std::visit([](auto &&cfg) -> command::ptr_t { return command::create(cfg); },
                    config);
}

}  // anonymous namespace

command::ptr_t cli_parse(int argc, char **argv) {
  CLI::App app{ "envy - freeform package manager" };

  bool show_version{ false };
  app.add_flag("-v,--version", show_version, "Show version information");

  std::optional<command_config> config;

  // Lua subcommand
  {
    auto *lua{ app.add_subcommand("lua", "Execute Lua script") };
    auto lua_cfg{ lua_command::config{} };

    lua->add_option("script", lua_cfg.script_path, "Lua script file to execute")
        ->required()
        ->check(CLI::ExistingFile);

    lua->callback([&config, lua_cfg]() { config = lua_cfg; });
  }

  // Playground subcommand
  {
    auto *playground{ app.add_subcommand("playground",
                                         "Run S3/Git/Curl playground demo") };
    auto playground_cfg{ playground_command::config{} };

    playground->add_option("s3_uri", playground_cfg.s3_uri, "S3 URI (s3://bucket/key)")
        ->required();
    playground->add_option("region", playground_cfg.region, "AWS region (optional)");

    playground->callback([&config, playground_cfg]() { config = playground_cfg; });
  }

  try {
    app.parse(argc, argv);
  } catch (CLI::CallForHelp const &) {
    std::cout << app.help() << '\n';
    return nullptr;
  } catch (CLI::ParseError const &e) {
    std::cerr << e.what() << '\n';
    return nullptr;
  }

  if (show_version) { return command::create(version_command::config{}); }

  if (!config) {
    std::cerr << app.help() << "\n";
    return nullptr;
  }

  return create_command(*config);
}

}  // namespace envy
