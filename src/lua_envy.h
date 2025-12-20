#pragma once

#include "sol/sol.hpp"

namespace envy {

// Registry index for options table (set by phase execution before calling verbs)
constexpr int ENVY_OPTIONS_RIDX = 100;

// Install envy globals, platform constants, and custom functions into Lua state
void lua_envy_install(sol::state &lua);

}  // namespace envy
