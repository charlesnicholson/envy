#pragma once

#include "oneapi/tbb/flow_graph.h"

#include <memory>

namespace envy {

class command {
 public:
  using ptr_t = std::unique_ptr<command>;

  virtual ~command() = default;
  virtual void schedule(tbb::flow::graph &g) = 0;

  template <typename config_type>
  static ptr_t create(config_type const &cfg);

 protected:
  command() = default;
};

// Command configs inherit from this for factory creation.
template <typename command_type>
struct command_cfg {
  using command_type_t = command_type;
};

template <typename config_type>
command::ptr_t command::create(config_type const &cfg) {
  using command_type = typename config_type::command_type_t;
  return std::make_unique<command_type>(cfg);
}

}  // namespace envy
