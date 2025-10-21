#include "cli.h"
#include "tui.h"

#include "tbb/flow_graph.h"
#include "tbb/task_arena.h"

#include <cstdlib>
#include <iostream>
#include <variant>

int main(int argc, char **argv) {
  envy::tui::init();

  try {
    auto args{ envy::cli_parse(argc, argv) };

    envy::tui::scope tui_scope{ args.verbosity };

    if (!args.cli_output.empty()) {
      if (args.cmd_cfg.has_value()) {
        envy::tui::info("%s", args.cli_output.c_str());
      } else {
        envy::tui::error("%s", args.cli_output.c_str());
        return EXIT_FAILURE;
      }
    }

    if (!args.cmd_cfg.has_value()) { return EXIT_FAILURE; }

    auto cmd{ std::visit([](auto const &cfg) { return envy::cmd::create(cfg); },
                         *args.cmd_cfg) };

    bool command_succeeded{ false };
    tbb::task_arena().execute([&cmd, &command_succeeded]() {
      tbb::flow::graph graph;
      cmd->schedule(graph);
      graph.wait_for_all();
      command_succeeded = cmd->succeeded();
      cmd.reset();  // graph must outlive nodes owned by command
    });

    return command_succeeded ? EXIT_SUCCESS : EXIT_FAILURE;
  } catch (std::exception const &ex) {
    std::cerr << "Execution failed: " << ex.what() << '\n';
    return EXIT_FAILURE;
  }
}
