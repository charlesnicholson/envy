#pragma once

#include "sol/sol.hpp"

namespace envy {

// Registry index for recipe options table (deserialized once per recipe Lua state)
constexpr int ENVY_OPTIONS_RIDX = 100;

// Install envy globals, platform constants, and custom functions into Lua state
void lua_envy_install(sol::state &lua);

}  // namespace envy
