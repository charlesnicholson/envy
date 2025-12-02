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

  // Override error() and assert() to automatically include stack traces
  lua->script(R"lua(
do
  local orig_error = error
  local orig_assert = assert

  _G.error = function(message, level)
    level = (level or 1) + 1
    return orig_error(debug.traceback(tostring(message), level), 0)
  end

  _G.assert = function(condition, message, ...)
    if not condition then
      message = message or "assertion failed"
      return orig_assert(false, debug.traceback(tostring(message), 2))
    end
    return condition, message, ...
  end
end
)lua");

  return lua;
}

}  // namespace envy
