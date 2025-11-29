#include "sol_util.h"

namespace envy {

sol_state_ptr sol_util_make_lua_state() {
  auto lua{ std::make_unique<sol::state>() };
  lua->open_libraries(sol::lib::base,
                      sol::lib::package,
                      sol::lib::coroutine,
                      sol::lib::string,
                      sol::lib::os,
                      sol::lib::math,
                      sol::lib::table,
                      sol::lib::debug,
                      sol::lib::bit32,
                      sol::lib::io);
  return lua;
}

}  // namespace envy
