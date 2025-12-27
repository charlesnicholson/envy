#include "cmd_lua.h"

#include "cache.h"
#include "lua_envy.h"
#include "sol_util.h"

#include "sol/sol.hpp"

namespace envy {

cmd_lua::cmd_lua(cmd_lua::cfg cfg, cache & /*c*/) : cfg_{ std::move(cfg) } {}

void cmd_lua::execute() {
  auto lua{ sol_util_make_lua_state() };
  lua_envy_install(*lua);

  sol::protected_function_result result =
      lua->safe_script_file(cfg_.script_path.string(), sol::script_pass_on_error);
  if (!result.valid()) {
    sol::error err = result;
    throw std::runtime_error(err.what());
  }
}

}  // namespace envy
