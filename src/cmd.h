#pragma once

#include "util.h"

#include <memory>

namespace envy {

class cmd : unmovable {
 public:
  using ptr_t = std::unique_ptr<cmd>;

  virtual ~cmd() = default;
  virtual bool execute() = 0;

  template <typename config>
  static ptr_t create(config const &cfg);

 protected:
  cmd() = default;
};

// Command configs inherit from this for factory creation.
template <typename command>
struct cmd_cfg {
  using cmd_t = command;
};

template <typename config>
cmd::ptr_t cmd::create(config const &cfg) {
  return std::make_unique<typename config::cmd_t>(cfg);
}

}  // namespace envy
