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

  std::optional<std::filesystem::path> cache_root;
  app.add_option("--cache-root", cache_root, "Cache root directory (overrides default)");

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

  auto const register_cmds{ [&]<typename... Ts>(CLI::App &parent) {
    (Ts::register_cli(parent, [&](auto c) { cmd_cfg = c; }), ...);
  } };

  register_cmds.operator()<cmd_version,
                           cmd_init,
                           cmd_package,
                           cmd_product,
                           cmd_shell,
                           cmd_sync,
                           cmd_extract,
                           cmd_fetch,
                           cmd_hash,
                           cmd_lua
#ifdef ENVY_FUNCTIONAL_TESTER
                           ,
                           cmd_engine_functional_test
#endif
                           >(app);

#ifdef ENVY_FUNCTIONAL_TESTER
  auto *cache{ app.add_subcommand("cache", "Cache testing commands") };
  register_cmds.operator()<cmd_cache_ensure_package, cmd_cache_ensure_spec>(*cache);
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
        args.trace_outputs.push_back({ tui::trace_output_type::std_err, std::nullopt });
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
      args.trace_outputs.push_back({ tui::trace_output_type::std_err, std::nullopt });
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
    args.cache_root = cache_root;
    return args;
  }

  if (cmd_cfg) {
    args.cmd_cfg = *cmd_cfg;
  } else if (args.cli_output.empty()) {
    args.cli_output = app.help();
  }

  args.cache_root = cache_root;
  return args;
}

}  // namespace envy
