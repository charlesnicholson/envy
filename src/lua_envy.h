#pragma once

#include "sol/sol.hpp"

namespace envy {

// Install envy globals, platform constants, and custom functions into Lua state
void lua_envy_install(sol::state &lua);

}  // namespace envy
