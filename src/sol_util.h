#pragma once

#include "sol/sol.hpp"

#include <memory>

namespace envy {

using sol_state_ptr = std::unique_ptr<sol::state>;
sol_state_ptr sol_util_make_lua_state();  // with std libs

}  // namespace envy
