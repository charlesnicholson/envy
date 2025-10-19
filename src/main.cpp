#include "cli.h"

#include "tbb/flow_graph.h"
#include "tbb/task_arena.h"

#include <cstdlib>
#include <iostream>

int main(int argc, char **argv) {
  try {
    auto cmd{ envy::cli_parse(argc, argv) };
    if (!cmd) { return EXIT_FAILURE; }

    tbb::task_arena().execute([&] {
      tbb::flow::graph graph;
      cmd->schedule(graph);
      graph.wait_for_all();
    });

    return EXIT_SUCCESS;
  } catch (std::exception const &ex) {
    std::cerr << "Execution failed: " << ex.what() << '\n';
    return EXIT_FAILURE;
  }
}
