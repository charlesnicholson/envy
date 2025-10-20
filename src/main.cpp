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
    auto args_opt{ envy::cli_parse(argc, argv) };
    if (!args_opt) { return EXIT_FAILURE; }

    auto args{ std::move(*args_opt) };
    auto cmd{ std::visit([](auto const &cfg) { return envy::cmd::create(cfg); },
                         args.cmd_cfg) };

    envy::tui::scope tui_scope{ args.verbosity };

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
