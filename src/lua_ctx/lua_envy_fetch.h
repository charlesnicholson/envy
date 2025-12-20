#pragma once

#include "sol/sol.hpp"

namespace envy {

// Install envy.fetch(), envy.commit_fetch(), and envy.verify_hash() into the envy table
void lua_envy_fetch_install(sol::table &envy_table);

}  // namespace envy
