#pragma once

#include "sol/sol.hpp"

namespace envy {

// Install envy.path.* utilities into the envy table
void lua_envy_path_install(sol::table &envy_table);

}  // namespace envy
