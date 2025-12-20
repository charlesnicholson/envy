#pragma once

#include "sol/sol.hpp"

namespace envy {

// Registry indices for per-state context (set by phase_context_guard)
constexpr int ENVY_OPTIONS_RIDX = 100;
constexpr int ENVY_ENGINE_RIDX = 101;  // engine* (lightuserdata)
constexpr int ENVY_RECIPE_RIDX = 102;  // recipe* (lightuserdata)

// Install envy globals, platform constants, and custom functions into Lua state
void lua_envy_install(sol::state &lua);

}  // namespace envy
