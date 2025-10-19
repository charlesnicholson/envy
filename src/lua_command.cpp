#include "lua_command.h"

namespace envy {

lua_command::lua_command(config cfg) : config_{ std::move(cfg) } {}

void lua_command::schedule(tbb::flow::graph &g) {
  // TODO: Implement Lua script execution
  (void)g;
}

}  // namespace envy
