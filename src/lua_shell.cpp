#include "lua_shell.h"

extern "C" {
#include "lauxlib.h"
}

#include <stdexcept>
#include <string>

namespace envy {

std::variant<shell_choice, custom_shell_file, custom_shell_inline>
parse_shell_config_from_lua(lua_State *L, int index, char const *context) {
  int const type{ lua_type(L, index) };

  // ENVY_SHELL constant (light userdata)
  if (type == LUA_TLIGHTUSERDATA) {
    void *ud{ lua_touserdata(L, index) };
    auto const choice{ static_cast<shell_choice>(reinterpret_cast<uintptr_t>(ud)) };

    // Validate enum value
    if (choice != shell_choice::bash && choice != shell_choice::sh &&
        choice != shell_choice::cmd && choice != shell_choice::powershell) {
      throw std::runtime_error(std::string{ context } + ": invalid ENVY_SHELL constant");
    }

    return choice;
  }

  // Custom shell table
  if (type == LUA_TTABLE) {
    try {
      // Push the table to top of stack if it's not already there
      // shell_parse_custom_from_lua expects table at -1
      lua_pushvalue(L, index);
      custom_shell custom{ shell_parse_custom_from_lua(L) };
      lua_pop(L, 1);  // Pop the copied table

      shell_validate_custom(custom);

      // Unpack custom_shell variant (variant<file, inline>) into return variant
      std::variant<shell_choice, custom_shell_file, custom_shell_inline> result;
      std::visit([&result](auto &&custom_cfg) { result = custom_cfg; }, custom);
      return result;
    } catch (std::exception const &e) {
      throw std::runtime_error(std::string{ context } + ": " + e.what());
    }
  }

  // Unsupported type
  throw std::runtime_error(std::string{ context } +
                           ": shell must be ENVY_SHELL constant or table {file=..., "
                           "ext=...} or {inline=...}");
}

}  // namespace envy
