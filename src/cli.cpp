#include "cli.h"
#include "tui.h"

#include "CLI11.hpp"

#include <optional>

namespace envy {

cli_args cli_parse(int argc, char **argv) {
  CLI::App app{ "envy - freeform package manager" };

  bool verbose{ false };
  app.add_flag("--verbose", verbose, "Enable structured verbose logging");

  // Support version flags (-v / --version) triggering version command directly.
  bool version_flag_short{ false };
  bool version_flag_long{ false };
  app.add_flag("-v",
               version_flag_short,
               "Show version information (alias for version subcommand)");
  app.add_flag("--version",
               version_flag_long,
               "Show version information (alias for version subcommand)");

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

  // Fetch subcommand
  cmd_fetch::cfg fetch_cfg{};
  auto *fetch{ app.add_subcommand("fetch", "Download resource to local file") };
  fetch->add_option("source", fetch_cfg.source, "Source URI (http/https/git/etc.)")
      ->required();
  fetch->add_option("destination", fetch_cfg.destination, "Destination file path")
      ->required();
  fetch->add_option("--manifest-root",
                    fetch_cfg.manifest_root,
                    "Manifest root for resolving relative file URIs");
  fetch->callback([&cmd_cfg, &fetch_cfg] { cmd_cfg = fetch_cfg; });

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
  playground->add_option("uri", playground_cfg.uri, "Resource URI (s3/http/file/...)")
      ->required();
  playground->add_option("region", playground_cfg.region, "AWS region (optional)");
  playground->callback([&cmd_cfg, &playground_cfg] { cmd_cfg = playground_cfg; });

#ifdef ENVY_FUNCTIONAL_TESTER
  // Cache testing subcommands
  auto *cache{ app.add_subcommand("cache", "Cache testing commands") };

  // cache ensure-asset
  cmd_cache_ensure_asset::cfg asset_cfg{};
  auto *ensure_asset{ cache->add_subcommand("ensure-asset", "Test asset cache entry") };
  ensure_asset->add_option("identity", asset_cfg.identity, "Asset identity")->required();
  ensure_asset
      ->add_option("platform", asset_cfg.platform, "Platform (darwin/linux/windows)")
      ->required();
  ensure_asset->add_option("arch", asset_cfg.arch, "Architecture (arm64/x86_64)")
      ->required();
  ensure_asset->add_option("hash_prefix", asset_cfg.hash_prefix, "Hash prefix")
      ->required();
  ensure_asset->add_option("--cache-root", asset_cfg.cache_root, "Cache root directory");
  ensure_asset->add_option("--test-id",
                           asset_cfg.test_id,
                           "Test ID for barrier isolation");
  ensure_asset->add_option("--barrier-dir", asset_cfg.barrier_dir, "Barrier directory");
  ensure_asset->add_option("--barrier-signal",
                           asset_cfg.barrier_signal,
                           "Barrier to signal before lock");
  ensure_asset->add_option("--barrier-wait",
                           asset_cfg.barrier_wait,
                           "Barrier to wait for before lock");
  ensure_asset->add_option("--barrier-signal-after",
                           asset_cfg.barrier_signal_after,
                           "Barrier to signal after lock");
  ensure_asset->add_option("--barrier-wait-after",
                           asset_cfg.barrier_wait_after,
                           "Barrier to wait for after lock");
  ensure_asset->add_option("--crash-after",
                           asset_cfg.crash_after_ms,
                           "Crash after N milliseconds");
  ensure_asset->add_flag("--fail-before-complete",
                         asset_cfg.fail_before_complete,
                         "Exit without marking complete");
  ensure_asset->callback([&cmd_cfg, &asset_cfg] { cmd_cfg = asset_cfg; });

  // cache ensure-recipe
  cmd_cache_ensure_recipe::cfg recipe_cfg{};
  auto *ensure_recipe{ cache->add_subcommand("ensure-recipe", "Test recipe cache entry") };
  ensure_recipe->add_option("identity", recipe_cfg.identity, "Recipe identity")
      ->required();
  ensure_recipe->add_option("--cache-root", recipe_cfg.cache_root, "Cache root directory");
  ensure_recipe->add_option("--test-id",
                            recipe_cfg.test_id,
                            "Test ID for barrier isolation");
  ensure_recipe->add_option("--barrier-dir", recipe_cfg.barrier_dir, "Barrier directory");
  ensure_recipe->add_option("--barrier-signal",
                            recipe_cfg.barrier_signal,
                            "Barrier to signal before lock");
  ensure_recipe->add_option("--barrier-wait",
                            recipe_cfg.barrier_wait,
                            "Barrier to wait for before lock");
  ensure_recipe->add_option("--barrier-signal-after",
                            recipe_cfg.barrier_signal_after,
                            "Barrier to signal after lock");
  ensure_recipe->add_option("--barrier-wait-after",
                            recipe_cfg.barrier_wait_after,
                            "Barrier to wait for after lock");
  ensure_recipe->add_option("--crash-after",
                            recipe_cfg.crash_after_ms,
                            "Crash after N milliseconds");
  ensure_recipe->add_flag("--fail-before-complete",
                          recipe_cfg.fail_before_complete,
                          "Exit without marking complete");
  ensure_recipe->callback([&cmd_cfg, &recipe_cfg] { cmd_cfg = recipe_cfg; });
#endif

  cli_args args{};

  try {
    app.parse(argc, argv);
  } catch (CLI::CallForHelp const &) {
    args.cli_output = app.help();
  } catch (CLI::ParseError const &e) { args.cli_output = std::string(e.what()); }

  if (verbose) {
    args.verbosity = tui::level::TUI_DEBUG;
    args.structured_logging = true;
  } else {
    args.verbosity = tui::level::TUI_INFO;
    args.structured_logging = false;
  }

  if (version_flag_short || version_flag_long) {
    args.cmd_cfg = cmd_version::cfg{};
    return args;
  }

  if (cmd_cfg) {
    args.cmd_cfg = *cmd_cfg;
  } else if (args.cli_output.empty()) {
    args.cli_output = app.help();
  }

  return args;
}

}  // namespace envy
