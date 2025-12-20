#pragma once

#include "sol/sol.hpp"

namespace envy {

// Install envy.* file operations into the envy table
void lua_envy_file_ops_install(sol::table &envy_table);

}  // namespace envy
