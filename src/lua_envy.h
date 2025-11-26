#pragma once

#include "sol/sol.hpp"

namespace envy {

// Registry index for recipe options table (deserialized once per recipe Lua state)
// Use a high number to avoid conflicts with Lua's reserved indices (1=mainthread, 2=globals)
constexpr int ENVY_OPTIONS_RIDX = 100;

// Install envy globals, platform constants, and custom functions into Lua state
void lua_envy_install(sol::state &lua);

}  // namespace envy
