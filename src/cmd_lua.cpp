#include "cmd_lua.h"
#include "lua_util.h"

namespace envy {

cmd_lua::cmd_lua(cmd_lua::cfg cfg) : cfg_{ std::move(cfg) } {}

bool cmd_lua::execute() {
  auto L{ lua_make() };
  if (!L) { return false; }
  lua_add_envy(L);
  return lua_run_file(L, cfg_.script_path);
}

}  // namespace envy
