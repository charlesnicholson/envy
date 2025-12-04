#pragma once

#include "shell.h"

namespace envy {

// Parse a shell configuration from a Lua sol2 object
// Accepts:
//   - ENVY_SHELL constant (light userdata): shell_choice enum
//   - Table: custom_shell (parsed via shell_parse_custom_from_lua)
// Returns variant containing shell_choice or custom_shell_file or custom_shell_inline
// Throws std::runtime_error with context prefix on parse/validation failure
//
// The context parameter is used for error messages (e.g., "ctx.run", "default_shell")
resolved_shell parse_shell_config_from_lua(sol::object const &obj, char const *context);

}  // namespace envy
