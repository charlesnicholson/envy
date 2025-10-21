#pragma once

#include "oneapi/tbb/flow_graph.h"

#include <memory>

namespace envy {

class cmd {
 public:
  using ptr_t = std::unique_ptr<cmd>;

  virtual ~cmd() = default;
  virtual void schedule(tbb::flow::graph &g) = 0;

  bool succeeded() const { return succeeded_; }

  template <typename config>
  static ptr_t create(config const &cfg);

 protected:
  cmd() = default;
  bool succeeded_{ true };
};

// Command configs inherit from this for factory creation.
template <typename command>
struct cmd_cfg {
  using cmd_t = command;
};

template <typename config>
cmd::ptr_t cmd::create(config const &cfg) {
  using command_t = typename config::cmd_t;
  return std::make_unique<command_t>(cfg);
}

}  // namespace envy
