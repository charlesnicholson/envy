#include "cli.h"
#include "tui.h"

#include "tbb/flow_graph.h"
#include "tbb/task_arena.h"

#include <cstdlib>
#include <iostream>

int main(int argc, char **argv) {
  envy::tui::init();

  try {
    auto cmd{ envy::cli_parse(argc, argv) };
    if (!cmd) { return EXIT_FAILURE; }

    struct tui_scope {
      tui_scope() { envy::tui::run(std::nullopt); }
      ~tui_scope() { envy::tui::shutdown(); }
    } scope;

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
