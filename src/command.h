#pragma once

#include <memory>

namespace tbb::flow { class graph; }

namespace envy {

class command {
 public:
  using ptr_t = std::unique_ptr<command>;

  virtual ~command() = default;
  virtual void schedule(tbb::flow::graph &g) = 0;

 protected:
  command() = default;
};

// Command configs inherit from this for factory creation.
template <typename command_type>
struct command_cfg {
  using command_type_t = command_type;
};

}  // namespace envy
