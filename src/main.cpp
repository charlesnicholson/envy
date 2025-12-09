#include "aws_util.h"
#include "cli.h"
#include "libgit2_util.h"
#include "shell.h"
#include "tui.h"

#include <cstdlib>
#include <variant>

int main(int argc, char **argv) {
  envy::tui::init();
  envy::shell_init();

  auto args{ envy::cli_parse(argc, argv) };
  envy::tui::configure_trace_outputs(args.trace_outputs);
  envy::tui::scope tui_scope{ args.verbosity, args.decorated_logging };

  envy::aws_shutdown_guard aws_guard;
  envy::libgit2_scope git_guard;

  if (!args.cli_output.empty()) {
    if (!args.cmd_cfg.has_value()) {
      envy::tui::error("%s", args.cli_output.c_str());
      return EXIT_FAILURE;
    }
    envy::tui::info("%s", args.cli_output.c_str());
  }

  if (!args.cmd_cfg.has_value()) { return EXIT_FAILURE; }

  auto cmd{ std::visit([](auto const &cfg) { return envy::cmd::create(cfg); },
                       *args.cmd_cfg) };

  bool ok{ false };
  try {
    ok = cmd->execute();
  } catch (std::exception const &ex) {
    envy::tui::error("Execution failed: %s", ex.what());
    return EXIT_FAILURE;
  }

  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
