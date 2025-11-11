#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

struct lua_State;

namespace envy {

using shell_env_t = std::unordered_map<std::string, std::string>;

struct shell_result {
  int exit_code;
  std::optional<int> signal;
};

enum class shell_choice { bash, sh, cmd, powershell };

// Custom shell configuration for file mode (script written to temp file)
struct custom_shell_file {
  std::vector<std::string> argv;  // Must be non-empty, first element is shell executable path
  std::string ext;                // Required, e.g. ".tcl", ".sh"
};

// Custom shell configuration for inline mode (script passed as command-line argument)
struct custom_shell_inline {
  std::vector<std::string> argv;  // Must be non-empty, first element is shell executable path
};

using custom_shell = std::variant<custom_shell_file, custom_shell_inline>;

// Manifest default_shell value (resolved to constant or custom shell)
using default_shell_value = std::variant<
  shell_choice,               // Built-in: ENVY_SHELL.BASH, etc.
  custom_shell                // Custom: {file = ..., ext = ...} or {inline = ...}
>;

// Manifest default_shell configuration (optional, nullopt if not specified)
using default_shell_cfg = std::optional<default_shell_value>;

struct shell_run_cfg {
  std::function<void(std::string_view)> on_output_line;
  std::optional<std::filesystem::path> cwd;
  shell_env_t env;
  std::variant<shell_choice, custom_shell_file, custom_shell_inline> shell{
#if defined(_WIN32)
    shell_choice::powershell
#else
    shell_choice::bash
#endif
  };
};

shell_choice shell_parse_choice(std::optional<std::string_view> value);

// Validate custom shell configuration (checks executable exists and is accessible)
void shell_validate_custom(custom_shell const &cfg);

// Parse custom shell from Lua table (expects table at top of stack, does not pop it)
custom_shell shell_parse_custom_from_lua(::lua_State *L);

shell_env_t shell_getenv();
shell_result shell_run(std::string_view script, shell_run_cfg const &cfg);

}  // namespace envy
