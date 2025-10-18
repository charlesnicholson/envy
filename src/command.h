#pragma once

namespace tbb::flow {
class graph;
}

namespace envy {

class Command {
 public:
  virtual ~Command() = default;
  virtual void Schedule(tbb::flow::graph &g) = 0;

 protected:
  Command() = default;
};

// Command configs inherit from this for factory creation.
template <typename CommandType_>
struct CommandConfig {
  using CommandType = CommandType_;
};

}  // namespace envy
