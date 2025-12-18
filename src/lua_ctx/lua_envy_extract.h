#pragma once

#include "sol/sol.hpp"

namespace envy {

// Install envy.extract() and envy.extract_all() into the envy table
void lua_envy_extract_install(sol::table &envy_table);

}  // namespace envy
