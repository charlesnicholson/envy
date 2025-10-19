#include "cmd_lua.h"

namespace envy {

cmd_lua::cmd_lua(cmd_lua::config cfg) : config_{ std::move(cfg) } {}

void cmd_lua::schedule(tbb::flow::graph &g) {
  // TODO: Implement Lua script execution
  (void)g;
}

}  // namespace envy
