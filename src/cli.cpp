#include "cli.h"
#include "tui.h"

#include "CLI11.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace envy {

cli_args cli_parse(int argc, char **argv) {
  CLI::App app{ "envy - freeform package manager" };
  // Disable Windows-style '/' option prefixes so absolute POSIX-style paths
  // like "/tmp/file" are treated as positional arguments on Windows.
  app.allow_windows_style_options(false);

  bool verbose{ false };
  app.add_flag(
      "--verbose",
      verbose,
      "Enable decorated verbose logging (prefix stdout/stderr with timestamp and level)");

  std::string trace_spec;
  auto *trace_option{ app.add_option("--trace",
                                     trace_spec,
                                     "Enable trace logging. Provide a comma-separated "
                                     "list: 'stderr' for human-readable stderr and/or "
                                     "'file:<path>' for JSONL file output. Defaults to "
                                     "stderr if no value provided.") };
  trace_option->expected(0, 1);

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

  // Asset subcommand
  cmd_asset::cfg query_asset_cfg{};
  auto *asset{ app.add_subcommand("asset",
                                  "Query and install package, print asset path") };
  asset
      ->add_option("identity",
                   query_asset_cfg.identity,
                   "Recipe identity (namespace.name@version)")
      ->required();
  asset->add_option("--manifest",
                    query_asset_cfg.manifest_path,
                    "Path to envy.lua manifest");
  asset->add_option("--cache-root", query_asset_cfg.cache_root, "Cache root directory");
  asset->callback([&cmd_cfg, &query_asset_cfg] { cmd_cfg = query_asset_cfg; });

  // Sync subcommand
  cmd_sync::cfg sync_cfg{};
  auto *sync{ app.add_subcommand("sync", "Install packages from manifest") };
  sync->add_option("identities",
                   sync_cfg.identities,
                   "Recipe identities to sync (sync all if omitted)");
  sync->add_option("--manifest", sync_cfg.manifest_path, "Path to envy.lua manifest");
  sync->add_option("--cache-root", sync_cfg.cache_root, "Cache root directory");
  sync->callback([&cmd_cfg, &sync_cfg] { cmd_cfg = sync_cfg; });

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
  fetch->add_option("--ref", fetch_cfg.ref, "Git ref (branch/tag/SHA) for git sources");
  fetch->callback([&cmd_cfg, &fetch_cfg] { cmd_cfg = fetch_cfg; });

  // Hash subcommand
  cmd_hash::cfg hash_cfg{};
  auto *hash{ app.add_subcommand("hash", "Compute SHA256 hash of a file") };
  hash->add_option("file", hash_cfg.file_path, "File to hash")
      ->required()
      ->check(CLI::ExistingFile);
  hash->callback([&cmd_cfg, &hash_cfg] { cmd_cfg = hash_cfg; });

  // Lua subcommand
  cmd_lua::cfg lua_cfg{};
  auto *lua{ app.add_subcommand("lua", "Execute Lua script") };
  lua->add_option("script", lua_cfg.script_path, "Lua script file to execute")
      ->required()
      ->check(CLI::ExistingFile);
  lua->callback([&cmd_cfg, &lua_cfg] { cmd_cfg = lua_cfg; });

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

  // Engine functional test subcommand
  cmd_engine_functional_test::cfg engine_test_cfg{};
  auto *engine_test{ app.add_subcommand("engine-test", "Test engine execution") };
  engine_test->add_option("identity", engine_test_cfg.identity, "Recipe identity")
      ->required();
  engine_test->add_option("recipe_path", engine_test_cfg.recipe_path, "Recipe file path")
      ->required()
      ->check(CLI::ExistingFile);
  engine_test->add_option("--cache-root",
                          engine_test_cfg.cache_root,
                          "Cache root directory");
  engine_test
      ->add_option("--fail-after-fetch-count",
                   engine_test_cfg.fail_after_fetch_count,
                   "Fail after N successful file downloads (test only)")
      ->default_val(-1);
  engine_test->callback([&cmd_cfg, &engine_test_cfg] { cmd_cfg = engine_test_cfg; });
#endif

  cli_args args{};

  try {
    app.parse(argc, argv);
  } catch (CLI::CallForHelp const &) {
    args.cli_output = app.help();
  } catch (CLI::ParseError const &e) { args.cli_output = std::string(e.what()); }

  // Handle trace logging: --trace defaults to stderr if no value provided
  bool const trace_requested{ trace_option->count() > 0 };
  std::vector<std::string> trace_specs_tokens;

  if (trace_requested) {
    if (trace_spec.empty()) {
      trace_specs_tokens.push_back("stderr");
    } else {
      for (std::string_view sv{ trace_spec }; !sv.empty();) {
        auto const pos{ sv.find(',') };
        auto const token{ sv.substr(0, pos) };
        if (!token.empty()) { trace_specs_tokens.emplace_back(token); }
        sv = (pos == std::string_view::npos) ? std::string_view{} : sv.substr(pos + 1);
      }
    }
  }

  if (!trace_specs_tokens.empty()) {
    args.verbosity = tui::level::TUI_TRACE;
    args.decorated_logging = true;
    for (auto const &spec : trace_specs_tokens) {
      if (spec.empty() || spec == "stderr") {
        args.trace_outputs.push_back({ tui::trace_output_type::stderr, std::nullopt });
      } else if (spec.rfind("file:", 0) == 0 && spec.size() > 5) {
        args.trace_outputs.push_back(
            { tui::trace_output_type::file, std::filesystem::path{ spec.substr(5) } });
      } else {
        args.cli_output = "Invalid trace output spec: " + spec;
        args.trace_outputs.clear();
        args.cmd_cfg.reset();
        cmd_cfg.reset();
        break;
      }
    }
    if (args.trace_outputs.empty() && args.cli_output.empty()) {
      args.trace_outputs.push_back({ tui::trace_output_type::stderr, std::nullopt });
    }
  } else if (verbose) {
    args.verbosity = tui::level::TUI_DEBUG;
    args.decorated_logging = true;
  } else {
    args.verbosity = tui::level::TUI_INFO;
    args.decorated_logging = false;
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
