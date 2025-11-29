#include "lua_shell.h"

#include <stdexcept>
#include <string>

namespace envy {

std::variant<shell_choice, custom_shell_file, custom_shell_inline>
parse_shell_config_from_lua(sol::object const &obj, char const *context) {
  // ENVY_SHELL constant (stored as number)
  if (obj.get_type() == sol::type::number) {
    int const value{ obj.as<int>() };
    auto const choice{ static_cast<shell_choice>(value) };

    // Validate enum value
    if (choice != shell_choice::bash && choice != shell_choice::sh &&
        choice != shell_choice::cmd && choice != shell_choice::powershell) {
      throw std::runtime_error(std::string{ context } + ": invalid ENVY_SHELL constant");
    }

    return choice;
  }

  // Custom shell table
  if (obj.is<sol::table>()) {
    try {
      custom_shell custom{ shell_parse_custom_from_lua(obj.as<sol::table>()) };
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
