#include "shell.h"

extern "C" {
#include "lua.h"
}

#include <filesystem>
#include <stdexcept>
#include <variant>
#include <vector>

namespace envy {

shell_choice shell_parse_choice(std::optional<std::string_view> value) {
#if defined(_WIN32)
  if (!value || value->empty()) { return shell_choice::powershell; }
  if (*value == "powershell") { return shell_choice::powershell; }
  if (*value == "cmd") { return shell_choice::cmd; }
  throw std::invalid_argument("shell option must be 'powershell' or 'cmd' on Windows");
#else
  if (!value || value->empty()) { return shell_choice::bash; }
  if (*value == "bash") { return shell_choice::bash; }
  if (*value == "sh") { return shell_choice::sh; }
  throw std::invalid_argument("shell option must be 'bash' or 'sh' on POSIX");
#endif
}

namespace {

template <typename T>
void validate_shell_config(T const &shell_cfg) {
  if (shell_cfg.argv.empty()) {
    throw std::runtime_error("Custom shell argv must be non-empty");
  }

  auto const shell_path{ std::filesystem::path{ shell_cfg.argv[0] } };

  // Check if shell executable exists
  std::error_code ec;
  if (!std::filesystem::exists(shell_path, ec)) {
    throw std::runtime_error("Custom shell executable not found: " + shell_path.string());
  }

  // Check if it's a regular file (not a directory)
  if (!std::filesystem::is_regular_file(shell_path, ec)) {
    throw std::runtime_error("Custom shell path is not a regular file: " +
                             shell_path.string());
  }

  // Note: We don't check executable permission on Windows (no reliable portable way)
  // On POSIX, we could use std::filesystem::perms, but that's also unreliable
  // (doesn't account for ACLs, setuid, etc.). Better to fail at execution time.
}

}  // namespace

void shell_validate_custom(custom_shell const &cfg) {
  std::visit([](auto const &shell_cfg) { validate_shell_config(shell_cfg); }, cfg);
}

custom_shell shell_parse_custom_from_lua(::lua_State *L) {
  if (!lua_istable(L, -1)) {
    throw std::runtime_error("custom shell must be a table with 'file' or 'inline' key");
  }

  // Check for 'file' key
  lua_getfield(L, -1, "file");
  bool const has_file{ !lua_isnil(L, -1) };
  lua_pop(L, 1);

  // Check for 'inline' key
  lua_getfield(L, -1, "inline");
  bool const has_inline{ !lua_isnil(L, -1) };
  lua_pop(L, 1);

  if (has_file && has_inline) {
    throw std::runtime_error(
        "custom shell table cannot have both 'file' and 'inline' keys");
  }
  if (!has_file && !has_inline) {
    throw std::runtime_error("custom shell table must have either 'file' or 'inline' key");
  }

  if (has_file) {
    // Parse file mode
    lua_getfield(L, -1, "file");

    // Handle string shorthand: file = "/path" → file = {"/path"}
    std::vector<std::string> argv;
    if (lua_isstring(L, -1)) {
      argv.push_back(lua_tostring(L, -1));
    } else if (lua_istable(L, -1)) {
      // Parse array of strings
      size_t const len{ lua_rawlen(L, -1) };
      if (len == 0) {
        throw std::runtime_error(
            "file mode argv must be non-empty (at least shell executable path)");
      }
      for (size_t i{ 1 }; i <= len; ++i) {
        lua_rawgeti(L, -1, static_cast<lua_Integer>(i));
        if (!lua_isstring(L, -1)) {
          throw std::runtime_error("file mode argv must contain only strings");
        }
        argv.push_back(lua_tostring(L, -1));
        lua_pop(L, 1);
      }
    } else {
      throw std::runtime_error("'file' key must be a string (path) or array of strings");
    }
    lua_pop(L, 1);  // Pop 'file' value

    // Get 'ext' field (required for file mode)
    lua_getfield(L, -1, "ext");
    if (!lua_isstring(L, -1)) {
      throw std::runtime_error("file mode requires 'ext' field (e.g., \".sh\", \".tcl\")");
    }
    std::string ext{ lua_tostring(L, -1) };
    lua_pop(L, 1);  // Pop 'ext' value

    return custom_shell_file{ std::move(argv), std::move(ext) };
  } else {
    // Parse inline mode
    lua_getfield(L, -1, "inline");

    std::vector<std::string> argv;
    if (lua_istable(L, -1)) {
      // Parse array of strings
      size_t const len{ lua_rawlen(L, -1) };
      if (len == 0) {
        throw std::runtime_error(
            "inline mode argv must be non-empty (at least shell executable path)");
      }
      for (size_t i{ 1 }; i <= len; ++i) {
        lua_rawgeti(L, -1, static_cast<lua_Integer>(i));
        if (!lua_isstring(L, -1)) {
          throw std::runtime_error("inline mode argv must contain only strings");
        }
        argv.push_back(lua_tostring(L, -1));
        lua_pop(L, 1);
      }
    } else {
      throw std::runtime_error("'inline' key must be an array of strings");
    }
    lua_pop(L, 1);  // Pop 'inline' value

    return custom_shell_inline{ std::move(argv) };
  }
}

}  // namespace envy
