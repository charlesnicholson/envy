#pragma once

#include "shell.h"

extern "C" {
#include "lua.h"
}

namespace envy {

// Parse a shell configuration from a Lua value at the given stack index
// Accepts:
//   - ENVY_SHELL constant (light userdata): shell_choice enum
//   - Table: custom_shell (parsed via shell_parse_custom_from_lua)
// Returns variant containing shell_choice or custom_shell_file or custom_shell_inline
// Throws std::runtime_error with context prefix on parse/validation failure
//
// The context parameter is used for error messages (e.g., "ctx.run", "default_shell")
// Does NOT pop the value from the stack
std::variant<shell_choice, custom_shell_file, custom_shell_inline>
parse_shell_config_from_lua(lua_State *L, int index, char const *context);

}  // namespace envy
