#pragma once

#include "sol/sol.hpp"

#include <string>

namespace envy {

// Install envy.options(schema) into the envy table
void lua_envy_options_install(sol::table &envy_table);

// Validate opts against a declarative schema table.
// Throws std::runtime_error on validation failure.
void validate_options_schema(sol::table const &schema,
                             sol::object const &opts,
                             std::string const &identity);

}  // namespace envy
