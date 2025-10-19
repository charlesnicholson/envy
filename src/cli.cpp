#include "cli.h"
#include "cmd_lua.h"
#include "cmd_playground.h"
#include "cmd_version.h"

#include "CLI11.hpp"

#include <iostream>
#include <optional>
#include <variant>

namespace envy {
namespace {

using command_config =
    std::variant<cmd_lua::config, cmd_playground::config, cmd_version::config>;

cmd::ptr_t create_command(command_config const &config) {
  return std::visit([](auto &&cfg) -> cmd::ptr_t { return cmd::create(cfg); }, config);
}

}  // anonymous namespace

cmd::ptr_t cli_parse(int argc, char **argv) {
  CLI::App app{ "envy - freeform package manager" };

  bool show_version{ false };
  app.add_flag("-v,--version", show_version, "Show version information");

  std::optional<command_config> config;
  auto const make_store_callback = [&config](auto &cfg) {
    return [&config, &cfg]() { config = cfg; };
  };

  // Lua subcommand
  cmd_lua::config lua_cfg{};
  {
    auto *lua{ app.add_subcommand("lua", "Execute Lua script") };

    lua->add_option("script", lua_cfg.script_path, "Lua script file to execute")
        ->required()
        ->check(CLI::ExistingFile);

    lua->callback(make_store_callback(lua_cfg));
  }

  // Playground subcommand
  cmd_playground::config playground_cfg{};
  {
    auto *playground{
        app.add_subcommand("playground", "Run S3/Git/Curl playground demo") };

    playground->add_option("s3_uri", playground_cfg.s3_uri, "S3 URI (s3://bucket/key)")
        ->required();
    playground->add_option("region", playground_cfg.region, "AWS region (optional)");

    playground->callback(make_store_callback(playground_cfg));
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

  if (show_version) { return cmd::create(cmd_version::config{}); }

  if (!config) {
    std::cerr << app.help() << "\n";
    return nullptr;
  }

  return create_command(*config);
}

}  // namespace envy
