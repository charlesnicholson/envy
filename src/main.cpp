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
    if (!args) { return EXIT_FAILURE; }

    envy::tui::scope tui_scope{ args->verbosity };

    auto cmd{ std::visit([](auto const &cfg) { return envy::cmd::create(cfg); },
                         args->cmd_cfg) };

    tbb::task_arena().execute([&cmd]() {
      tbb::flow::graph graph;
      cmd->schedule(graph);
      graph.wait_for_all();
      cmd.reset();  // graph must outlive nodes owned by command
    });

    return EXIT_SUCCESS;
  } catch (std::exception const &ex) {
    std::cerr << "Execution failed: " << ex.what() << '\n';
    return EXIT_FAILURE;
  }
}
