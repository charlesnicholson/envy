#pragma once

#include "cmd.h"

#include <functional>

namespace CLI { class App; }

namespace envy {

// Functional-tester-only: dump the trace event registry as JSON so Python tests
// can verify the parser registry matches the binary.
class cmd_trace_schema : public cmd {
 public:
  struct cfg : cmd_cfg<cmd_trace_schema> {};

  static void register_cli(CLI::App &app, std::function<void(cfg)> on_selected);

  cmd_trace_schema(cfg cfg, std::optional<std::filesystem::path> const &cli_cache_root);

  void execute() override;
};

}  // namespace envy
