#pragma once

#include "sol/sol.hpp"

namespace envy {

// Install envy.asset() and envy.product() functions into the envy table.
// These use thread-local context from lua_phase_context to access engine/recipe.
void lua_envy_deps_install(sol::table &envy_table);

}  // namespace envy
