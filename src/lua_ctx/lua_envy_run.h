#pragma once

#include "sol/sol.hpp"

namespace envy {

// Install envy.run() into the envy table
void lua_envy_run_install(sol::table &envy_table);

}  // namespace envy
