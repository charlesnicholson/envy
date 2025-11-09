#include "shell.h"

#include <stdexcept>

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

}  // namespace envy
