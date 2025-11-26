#include "cmd_lua.h"

#include "lua_envy.h"
#include "tui.h"

#include "sol/sol.hpp"

namespace envy {

cmd_lua::cmd_lua(cmd_lua::cfg cfg) : cfg_{ std::move(cfg) } {}

bool cmd_lua::execute() {
  sol::state lua;
  lua.open_libraries(sol::lib::base,
                     sol::lib::package,
                     sol::lib::coroutine,
                     sol::lib::string,
                     sol::lib::os,
                     sol::lib::math,
                     sol::lib::table,
                     sol::lib::debug,
                     sol::lib::bit32,
                     sol::lib::io);
  lua_envy_install(lua);

  sol::protected_function_result result =
      lua.safe_script_file(cfg_.script_path.string(), sol::script_pass_on_error);
  if (!result.valid()) {
    sol::error err = result;
    tui::error("%s", err.what());
    return false;
  }
  return true;
}

}  // namespace envy
