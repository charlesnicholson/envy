#pragma once

#include "sol/sol.hpp"

namespace envy {

// Install envy.package() and envy.product() functions into the envy table.
// These use thread-local context from lua_phase_context to access engine/pkg.
void lua_envy_deps_install(sol::table &envy_table);

}  // namespace envy
